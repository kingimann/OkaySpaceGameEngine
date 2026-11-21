// OGRE skeleton (.skeleton.xml) reading and the skinning of .mesh.xml bone assignments. SYNTHETIC fixtures with every number
// worked out by hand (see the comments); written from the public OGRE XML skeleton description. Builds on its own
// (OKAY_STANDALONE_TEST, reader source only).
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

// Root (id 0) at (0,1,0). Arm (id 1) child of Root at (2,0,0) turned 90 degrees about Z. Hand (id 2) child of Arm at (1,0,0).
// E1 (id 3) and E2 (id 4) children of Root at the origin. World bind positions: Root (0,1,0), Arm (2,1,0),
// Hand = Arm * (1,0,0) = (2,1,0) + Rz90 (1,0,0) = (2,2,0).
static const char* kSkeleton = R"(<?xml version="1.0"?>
<skeleton>
  <bones>
    <bone id="0" name="Root"><position x="0" y="1" z="0"/><rotation angle="0"><axis x="1" y="0" z="0"/></rotation></bone>
    <bone id="1" name="Arm"><position x="2" y="0" z="0"/><rotation angle="1.5707963267948966"><axis x="0" y="0" z="1"/></rotation></bone>
    <bone id="2" name="Hand"><position x="1" y="0" z="0"/><rotation angle="0"><axis x="1" y="0" z="0"/></rotation></bone>
    <bone id="3" name="E1"><position x="0" y="0" z="0"/></bone>
    <bone id="4" name="E2"><position x="0" y="0" z="0"/></bone>
  </bones>
  <bonehierarchy>
    <boneparent bone="Arm" parent="Root"/>
    <boneparent bone="Hand" parent="Arm"/>
    <boneparent bone="E1" parent="Root"/>
    <boneparent bone="E2" parent="Root"/>
  </bonehierarchy>
  <animations>
    <animation name="wave" length="2">
      <tracks>
        <track bone="Arm">
          <keyframes>
            <keyframe time="0"><translate x="0" y="0" z="0"/><rotate angle="0"><axis x="0" y="0" z="1"/></rotate><scale x="1" y="1" z="1"/></keyframe>
            <keyframe time="1"><translate x="0" y="3" z="0"/><rotate angle="1.5707963267948966"><axis x="0" y="0" z="1"/></rotate><scale x="2" y="1" z="1"/></keyframe>
            <keyframe time="2"/>
          </keyframes>
        </track>
        <track bone="Nobody"><keyframes><keyframe time="0"/></keyframes></track>
      </tracks>
    </animation>
  </animations>
</skeleton>
)";

// One triangle over three vertices. v0: Root 1. v1: five assignments (Arm .4, Hand .3, Root .2, E1 .06, E2 .04). v2: none
// that are usable (a bone id that does not exist). Two more assignments are bad (vertex 9 does not exist).
static std::string MeshXml() {
    return R"(<mesh>
  <submeshes><submesh material="M" usesharedvertices="false" use32bitindexes="false" operationtype="triangle_list">
    <faces count="1"><face v1="0" v2="1" v3="2"/></faces>
    <geometry vertexcount="3"><vertexbuffer positions="true">
      <vertex><position x="0" y="0" z="0"/></vertex>
      <vertex><position x="1" y="0" z="0"/></vertex>
      <vertex><position x="0" y="1" z="0"/></vertex>
    </vertexbuffer></geometry>
    <boneassignments>
      <vertexboneassignment vertexindex="0" boneindex="0" weight="1"/>
      <vertexboneassignment vertexindex="1" boneindex="1" weight="0.4"/>
      <vertexboneassignment vertexindex="1" boneindex="2" weight="0.3"/>
      <vertexboneassignment vertexindex="1" boneindex="0" weight="0.2"/>
      <vertexboneassignment vertexindex="1" boneindex="3" weight="0.06"/>
      <vertexboneassignment vertexindex="1" boneindex="4" weight="0.04"/>
      <vertexboneassignment vertexindex="9" boneindex="0" weight="1"/>
      <vertexboneassignment vertexindex="2" boneindex="77" weight="1"/>
    </boneassignments>
  </submesh></submeshes>
  <skeletonlink name="x.skeleton"/>
</mesh>
)";
}

int main() {
    RUN_SUITE("ogre_skeleton");
    {   // the skeleton alone: nodes, bind pose, clips
        ImportedModel m; std::string err;
        CHECK(ReadOgreSkeletonXml(kSkeleton, m, &err));
        CHECK(m.backend == "own" && m.parts.empty() && m.nodes.size() == 6);
        CHECK(m.nodes[0].parent == -1 && m.nodes[1].name == "Root" && m.nodes[1].parent == 0);
        CHECK(m.nodes[2].name == "Arm" && m.nodes[2].parent == 1 && m.nodes[3].name == "Hand" && m.nodes[3].parent == 2);
        CHECK(Near(m.nodes[1].position.y, 1) && Near(m.nodes[2].position.x, 2));
        CHECK(Near(m.nodes[2].rotation.z, std::sqrt(0.5f)) && Near(m.nodes[2].rotation.w, std::sqrt(0.5f)));
        CHECK(m.clips.size() == 1 && m.clips[0].name == "wave");
        CHECK(m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "Arm");
        const ImportedChannel& c = m.clips[0].channels[0];
        CHECK(c.position.size() == 3 && c.rotation.size() == 3 && c.scale.size() == 3);
        // keyframe 0 is the bind pose; keyframe 1 adds (0,3,0), turns a further 90 degrees (180 in all: z 1, w 0), doubles x
        CHECK(Near(c.position[0].value.x, 2) && Near(c.position[0].value.y, 0) && Near(c.position[0].time, 0));
        CHECK(Near(c.position[1].value.x, 2) && Near(c.position[1].value.y, 3) && Near(c.position[1].time, 1));
        CHECK(Near(c.rotation[1].value.z, 1) && Near(c.rotation[1].value.w, 0, 1e-3f));
        CHECK(Near(c.scale[1].value.x, 2) && Near(c.scale[1].value.y, 1));
        CHECK(Near(c.position[2].time, 2) && Near(c.position[2].value.y, 0));   // a bare <keyframe/> is the bind pose
        CHECK(HasNote(m, "Nobody"));
    }
    {   // the mesh skinned by it
        ImportedModel m; std::string err;
        const std::string skel = kSkeleton;
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err, nullptr, &skel));
        CHECK(m.parts.size() == 1 && m.nodes.size() == 6 && m.clips.size() == 1);
        const ImportedPart& p = m.parts[0];
        CHECK(p.mesh.vertices.size() == 3 && p.jointIdx.size() == 3 && p.jointWt.size() == 3);
        // bones: Root, Arm, Hand, E1, E2, then "OGRE" (v2 had no usable weight)
        CHECK(p.bones.size() == 6 && p.bones[0].name == "Root" && p.bones[1].name == "Arm" && p.bones[2].name == "Hand" && p.bones[5].name == "OGRE");
        // inverse bind of Arm (world (2,1,0), turned 90 about Z): the world point (2,2,0) is (0,1,0) in the bone's frame
        // rotated back: Rz(-90) * (0,1,0) = (1,0,0)
        const Vec3 a = p.bones[1].inverseBind.MultiplyPoint(Vec3{2, 2, 0});
        CHECK(Near(a.x, 1) && Near(a.y, 0) && Near(a.z, 0));
        const Vec3 h = p.bones[2].inverseBind.MultiplyPoint(Vec3{2, 2, 0});   // Hand sits there
        CHECK(Near(h.x, 0) && Near(h.y, 0) && Near(h.z, 0));
        const Vec3 r = p.bones[0].inverseBind.MultiplyPoint(Vec3{0, 1, 0});
        CHECK(Near(r.x, 0) && Near(r.y, 0));
        for (std::size_t i = 0; i < 3; ++i) {
            const Vec3 v = p.mesh.vertices[i];
            if (v.x == 0 && v.y == 0) { CHECK(p.jointIdx[i][0] == 0 && Near(p.jointWt[i][0], 1) && Near(p.jointWt[i][1], 0)); }
            else if (v.x == 1) {   // four strongest of five, renormalised by their sum 0.96
                CHECK(p.jointIdx[i][0] == 1 && p.jointIdx[i][1] == 2 && p.jointIdx[i][2] == 0 && p.jointIdx[i][3] == 3);
                CHECK(Near(p.jointWt[i][0], 0.4f / 0.96f) && Near(p.jointWt[i][1], 0.3f / 0.96f) && Near(p.jointWt[i][2], 0.2f / 0.96f) && Near(p.jointWt[i][3], 0.06f / 0.96f));
            } else { CHECK(p.jointIdx[i][0] == 5 && Near(p.jointWt[i][0], 1)); }   // the unweighted one goes to the root node
        }
        CHECK(HasNote(m, "bone assignment") && !HasNote(m, "not read: the mesh is imported at its bind pose"));
    }
    {   // no skeleton given: as before, rigid with the old note
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].bones.empty() && m.clips.empty() && HasNote(m, "skeleton 'x.skeleton'"));
    }
    {   // a skeleton that cannot be read: the mesh still loads rigid, and says why
        ImportedModel m; std::string err;
        const std::string bad = "<skeleton><bones><bone id=\"0\" name=\"A\"/><bone id=\"0\" name=\"B\"/></bones></skeleton>";
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", m, &err, nullptr, &bad));
        CHECK(m.parts.size() == 1 && m.parts[0].bones.empty() && HasNote(m, "skeleton could not be read"));
    }
    {   // refusals of the skeleton reader
        ImportedModel m; std::string err;
        CHECK(!ReadOgreSkeletonXml("<mesh/>", m, &err));
        CHECK(!ReadOgreSkeletonXml("<skeleton/>", m, &err));
        CHECK(!ReadOgreSkeletonXml("<skeleton><bones><bone id=\"0\" name=\"A\"/><bone id=\"1\" name=\"A\"/></bones></skeleton>", m, &err));
        CHECK(!ReadOgreSkeletonXml("<skeleton><bones><bone id=\"0\" name=\"A\"/></bones><bonehierarchy><boneparent bone=\"A\" parent=\"Z\"/></bonehierarchy></skeleton>", m, &err));
        CHECK(!ReadOgreSkeletonXml("<skeleton><bones><bone id=\"0\" name=\"A\"/><bone id=\"1\" name=\"B\"/></bones><bonehierarchy><boneparent bone=\"A\" parent=\"B\"/><boneparent bone=\"B\" parent=\"A\"/></bonehierarchy></skeleton>", m, &err));
        CHECK(m.nodes.empty() && m.clips.empty());
        // children listed before their parents still come out parent-first
        CHECK(ReadOgreSkeletonXml("<skeleton><bones><bone id=\"0\" name=\"Kid\"><position x=\"1\" y=\"0\" z=\"0\"/></bone><bone id=\"1\" name=\"Mum\"/></bones><bonehierarchy><boneparent bone=\"Kid\" parent=\"Mum\"/></bonehierarchy></skeleton>", m, &err));
        CHECK(m.nodes.size() == 3 && m.nodes[1].name == "Mum" && m.nodes[2].name == "Kid" && m.nodes[2].parent == 1);
        // every truncation of the good file either reads cleanly or is refused with nothing left behind
        const std::string good = kSkeleton;
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadOgreSkeletonXml(good.substr(0, n), t, &e);
            CHECK(ok || (!e.empty() && t.nodes.empty() && t.clips.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
