#pragma once
// ModelingToolIndex: the Modeling window as data. Every tool the window offers is listed once (its page, the
// section it sits in, search words and a short plain hint), the pages are grouped by job (Create, Shape and size, Edit parts,
// Effects, Files), and search finds tools by name, alias or section. The window draws from this: the sidebar headings, the
// search box, the highlight on matching buttons and the hint line. The exact technical names stay (Bevel, Extrude ...): the
// "no jargon" rule is for the Easy page only (page 8, the first page, the default). No ImGui, no engine. Written from scratch for this engine.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace oked::modeltools {

struct Page { int legacy; const char* name; const char* desc; const char* job; const char* abbr; const char* was; };   // abbr: the two letters on the folded strip; was: the old technical title (still found by search); legacy = the page number the window and OKAY_UI_MODELPAGE use
/// The pages in the order of the sidebar, under their job headings. Titles are plain words for a beginner.
inline const std::vector<Page>& Pages() {
    static const std::vector<Page> p = {
        {8, "Easy",             "Big buttons, start here",   "Start here",     "Ea", "Simple view"},
        {0, "Add shapes",       "Shapes, trees, props",  "Make something", "As", "Add"},
        {7, "Step by step",     "Fences, buildings, cars",    "Make something", "St", "Guided"},
        {4, "Make shapes",      "Spin, pipes and curves",     "Make something", "Ma", "Generate"},
        {1, "Change the shape", "Size, colour, solid body",   "Change it",      "Ch", "Shape"},
        {2, "Bend and cut",     "Bend, cut, mirror",  "Change it",      "Be", "Edit"},
        {5, "Fix the surface",  "Pick, sculpt, paint parts",  "Fix parts",      "Fi", "Edit Mesh"},
        {3, "Effects",          "Copies, joins, simpler",     "Extras",         "Ef", "Modifiers"},
        {6, "Bring in a model", "Bring in and save files",    "Files",          "Br", "Import"},
    };
    return p;
}
inline const Page* FindPage(int legacy) { for (const Page& p : Pages()) if (p.legacy == legacy) return &p; return nullptr; }

struct Tool { const char* id; const char* name; int page; const char* section; const char* aliases; const char* hint; };
inline const std::vector<Tool>& Tools() {
    static const std::vector<Tool> t = {
    {"p1:Randomize##variant", "Randomize", 1, "Primitive", "", ""},
    {"p1:Wireframe##model", "Wireframe", 1, "Primitive", "lines edges only", "Show or build edges as bars."},
    {"p1:Box (bounds)##colmode", "Box (bounds)", 1, "Primitive", "", ""},
    {"p1:Mesh (exact)##colmode", "Mesh (exact)", 1, "Primitive", "", ""},
    {"p1:None##colmode", "None", 1, "Primitive", "", ""},
    {"p2:X##loop", "Slice X", 2, "Detail", "", ""},
    {"p2:Y##loop", "Slice Y", 2, "Detail", "", ""},
    {"p2:Z##loop", "Slice Z", 2, "Detail", "", ""},
    {"p2:Relax##model", "Relax", 2, "Clean up", "smooth soften", "Even out bumps by moving corners toward their neighbours."},
    {"p2:Fill Holes##model", "Fill Holes", 2, "Clean up", "close gaps cap", "Cap open holes."},
    {"p2:Bridge##model", "Bridge", 2, "Clean up", "join connect", "Connect two open edges with faces."},
    {"p2:Snap Grid##model", "Snap Grid", 2, "Shape", "round positions grid", "Snap every corner to the grid."},
    {"p2:Shade Smooth##model", "Shade Smooth", 2, "Shading", "normals soft shading", "Smooth shading across faces."},
    {"p2:Flat##shade", "Flat", 2, "Shading", "", ""},
    {"p2:Auto Smooth##model", "Auto Smooth", 2, "Shading", "normals angle", "Smooth shading below an angle."},
    {"p2:Jitter##model", "Jitter", 2, "Deform", "random noise", "Nudge corners randomly."},
    {"p2:Shear X##model", "Shear X", 2, "Deform", "", ""},
    {"p2:Z##shear", "Shear Z", 2, "Deform", "", ""},
    {"p2:Export OBJ##model", "Export OBJ", 2, "Pivot & export", "save obj file export", "Write the model as an .obj file."},
    {"p2:Box##uv", "Box", 2, "UV Unwrap", "", ""},
    {"p2:Planar Y##uv", "Planar Y", 2, "UV Unwrap", "", ""},
    {"p2:Cylinder##uv", "Cylinder", 2, "UV Unwrap", "", ""},
    {"p2:Sphere##uv", "Sphere", 2, "UV Unwrap", "", ""},
    {"p2:Mirror X##model", "Mirror X", 2, "Symmetry, deform and shell", "symmetry flip copy", "Add a mirrored copy across X."},
    {"p2:Mirror Y##model", "Mirror Y", 2, "Symmetry, deform and shell", "symmetry flip copy", "Add a mirrored copy across Y."},
    {"p2:Mirror Z##model", "Mirror Z", 2, "Symmetry, deform and shell", "symmetry flip copy", "Add a mirrored copy across Z."},
    {"p2:Spherify##model", "Spherify", 2, "Symmetry, deform and shell", "round ball", "Push the model onto a sphere."},
    {"p2:Twist Y##model", "Twist Y", 2, "Symmetry, deform and shell", "deform spiral", "Twist the model around Y."},
    {"p2:Solidify##model", "Solidify", 2, "Symmetry, deform and shell", "thickness hollow shell wall", "Give the surface a thickness."},
    {"p2:Taper Y##model", "Taper Y", 2, "Symmetry, deform and shell", "deform narrow", "Narrow the model along Y."},
    {"p2:Bend X##model", "Bend X", 2, "Symmetry, deform and shell", "deform curve", "Bend the model."},
    {"p3:Array X##model", "Array X", 3, "Modifiers", "repeat copies duplicate", "Repeat copies along X."},
    {"p3:Remesh##model", "Remesh", 3, "Modifiers", "retopo quads rebuild", "Rebuild the surface evenly."},
    {"p3:Decimate##model", "Decimate", 3, "Modifiers", "reduce simplify low poly fewer triangles", "Make the model lighter by merging detail."},
    {"p3:Union##bool", "Union", 3, "Modifiers", "boolean join add", "Join two models into one."},
    {"p3:Subtract##bool", "Subtract", 3, "Modifiers", "boolean cut hole difference", "Cut one model out of another."},
    {"p3:Intersect##bool", "Intersect", 3, "Modifiers", "boolean overlap common", "Keep only the overlap."},
    {"p3:Convex Hull##model", "Convex Hull", 3, "Modifiers", "wrap shrink wrap", "Wrap the model in its simplest outer shape."},
    {"p3:Shrink/Fatten##model", "Shrink/Fatten", 3, "Modifiers", "", ""},
    {"p3:Wireframe##model", "Wireframe", 3, "Modifiers", "lines edges only", "Show or build edges as bars."},
    {"p3:Displace##model", "Displace", 3, "Modifiers", "noise bump", "Push corners in and out."},
    {"p3:Cast Cyl Y##model", "Cast Cyl Y", 3, "Modifiers", "", ""},
    {"p3:Stretch X##model", "Stretch X", 3, "Modifiers", "", ""},
    {"p3:Y##str", "Stretch Y", 3, "Modifiers", "", ""},
    {"p3:Z##str", "Stretch Z", 3, "Modifiers", "", ""},
    {"p4:Remove", "Remove", 4, "Procedural", "", ""},
    {"p4:+ Point##gen", "Add profile point", 4, "Procedural", "", ""},
    {"p4:Vase##gen", "Vase", 4, "Procedural", "", ""},
    {"p4:Goblet##gen", "Goblet", 4, "Procedural", "", ""},
    {"p4:Column##gen", "Column", 4, "Procedural", "", ""},
    {"p4:Lathe##gen", "Lathe", 4, "Procedural", "revolve spin profile", "Spin a profile around an axis."},
    {"p4:Screw##gen", "Screw", 4, "Procedural", "helix spiral", "Spin and lift a profile."},
    {"p4:Extrude Outline##gen", "Extrude Outline", 4, "Procedural", "extrude profile", "Pull a 2D outline into a solid."},
    {"p4:Show curve in the 3D view##curveshow", "Show curve in the 3D view", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Closed loop##curveclosed", "Closed loop", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:+ Point##curveadd", "Add curve point", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Arc##curvearc", "Arc", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:S##curves", "Curve S", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Spiral##curvesp", "Spiral", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Loop##curveloop", "Loop", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:From Selected Edges##curveedges", "From Selected Edges", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:End caps##ccaps", "End caps", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Make Pipe##curvepipe", "Make Pipe", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Make Ribbon##curverib", "Make Ribbon", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Sweep Profile##curvesweep", "Sweep Profile", 4, "Bezier curve (pipe, ribbon, swept profile)", "", ""},
    {"p4:Pipe##gen", "Pipe", 4, "Pipe (sweep along path)", "", ""},
    {"p5:Edit Mesh##model", "Edit Mesh", 5, "Pick parts", "", ""},
    {"p5:Select All##me", "Select All", 5, "Pick parts", "", ""},
    {"p5:Deselect##me", "Deselect", 5, "Pick parts", "", ""},
    {"p5:Grow##mesel", "Grow", 5, "Pick parts", "expand more select", "Select the neighbours too."},
    {"p5:Shrink##mesel", "Shrink", 5, "Pick parts", "less select", "Select less around the edge."},
    {"p5:Linked##mesel", "Linked", 5, "Pick parts", "whole part connected select island", "Select everything connected."},
    {"p5:Invert##mesel", "Invert", 5, "Pick parts", "select", "Swap picked and unpicked."},
    {"p5:Checker##me", "Checker", 5, "Pick parts", "checker deselect every other alternate", "Keep every other picked corner or face."},
    {"p5:Boundary Loop##me", "Boundary Loop", 5, "Pick parts", "outline region to loop border select", "Pick the outline of the picked faces as edges."},
    {"p5:Open Edges##me", "Open Edges", 5, "Pick parts", "holes border rim boundary select", "Pick every edge at the rim of a hole."},
    {"p5:Non-Manifold##me", "Non-Manifold", 5, "Pick parts", "broken edges three faces fin select", "Pick edges shared by three or more faces."},
    {"p5:Symmetry X##me", "Symmetry X", 5, "Pick parts", "", ""},
    {"p5:Y##mesymy", "Symmetry Y", 5, "Pick parts", "", ""},
    {"p5:Z##mesymz", "Symmetry Z", 5, "Pick parts", "", ""},
    {"p5:X##symdx", "Start with Symmetry X", 5, "Pick parts", "", ""},
    {"p5:Y##symdy", "Start with Symmetry Y", 5, "Pick parts", "", ""},
    {"p5:Z##symdz", "Start with Symmetry Z", 5, "Pick parts", "", ""},
    {"p5:Soft Select##me", "Soft Select", 5, "Pick parts", "", ""},
    {"p5:Extrude Faces##me", "Extrude Faces", 5, "Faces", "push out pull out", "Pull the picked faces out as a new block (extrude)."},
    {"p5:Inset Faces##me", "Inset Faces", 5, "Faces", "shrink border window", "Add a smaller face inside the picked face (inset)."},
    {"p5:Subdivide Sel##me", "Subdivide Sel", 5, "Faces", "", ""},
    {"p5:Poke##me", "Poke", 5, "Faces", "pyramid", "Turn a face into a pointed cone."},
    {"p5:Extrude Indiv##me", "Extrude Indiv", 5, "Faces", "push out each", "Extrude each picked face on its own."},
    {"p5:Inset Indiv##me", "Inset Indiv", 5, "Faces", "shrink each", "Inset each picked face on its own."},
    {"p5:Delete Faces##me", "Delete Faces", 5, "Faces", "remove", "Remove the picked faces."},
    {"p5:Flip Normals##me", "Flip Normals", 5, "Faces", "invert inside out", "Turn faces to point the other way."},
    {"p5:Recalc Outside##me", "Recalc Outside", 5, "Faces", "normals fix direction", "Make all faces point outward."},
    {"p5:Merge by Distance##me", "Merge by Distance", 5, "Faces", "weld join close", "Merge corners that are very close."},
    {"p5:Repair Mesh##me", "Repair Mesh", 5, "Faces", "fix problems holes", "Fix holes and flipped faces."},
    {"p5:Bevel Sel##me", "Bevel Sel", 5, "Corners and edges", "round corners chamfer", "Cut back the picked corners."},
    {"p5:Relax Sel##me", "Relax Sel", 5, "Corners and edges", "", ""},
    {"p5:Shrink/Fatten##me", "Shrink/Fatten", 5, "Corners and edges", "", ""},
    {"p5:To Sphere##me", "To Sphere", 5, "Corners and edges", "", ""},
    {"p5:X##fl", "Flatten X", 5, "Corners and edges", "", ""},
    {"p5:Y##fl", "Flatten Y", 5, "Corners and edges", "", ""},
    {"p5:Z##fl", "Flatten Z", 5, "Corners and edges", "", ""},
    {"p5:Collapse Edges##me", "Collapse Edges", 5, "Corners and edges", "", ""},
    {"p5:Split Edges##me", "Split Edges", 5, "Corners and edges", "", ""},
    {"p5:Extrude Edges##me", "Extrude Edges", 5, "Corners and edges", "pull edge", "Pull the picked edges out into new faces."},
    {"p5:Duplicate Sel##me", "Duplicate Sel", 5, "Corners and edges", "copy clone", "Copy the picked faces."},
    {"p5:Find a Tool... (Ctrl+K)##mecmd", "Find a Tool... (Ctrl+K)", 5, "Corners and edges", "", ""},
    {"p5:Repeat Last (Shift+R)##mecmd", "Repeat Last (Shift+R)", 5, "Corners and edges", "", ""},
    {"p5:Snap (Ctrl)##msnap", "Snap (Ctrl)", 5, "Corners and edges", "", ""},
    {"p5:All quads##mequad2", "All quads", 5, "Corners and edges", "", ""},
    {"p5:Snap Sel##me", "Snap Sel", 5, "Corners and edges", "", ""},
    {"p5:Select Loop##me", "Select Loop", 5, "Edge loops and joins", "edge loop pick", "Select a whole ring of edges."},
    {"p5:Select Ring##me", "Select Ring", 5, "Edge loops and joins", "pick", "Select parallel edges."},
    {"p5:Loop Cut##me", "Loop Cut", 5, "Edge loops and joins", "add cut ring edge loop", "Add a ring of new edges around the model (Ctrl+R)."},
    {"p5:Edge Slide##me", "Edge Slide", 5, "Edge loops and joins", "slide loop", "Slide the picked edges along their neighbours."},
    {"p5:Bevel##me", "Bevel", 5, "Edge loops and joins", "round edges chamfer soften corners", "Round off sharp edges (a bevel)."},
    {"p5:Knife##me", "Knife", 5, "Edge loops and joins", "cut slice", "Draw a cut line across faces (K)."},
    {"p5:Dissolve Edges##me", "Dissolve Edges", 5, "Edge loops and joins", "remove delete merge", "Remove edges and merge the faces."},
    {"p5:Dissolve Verts##me", "Dissolve Verts", 5, "Edge loops and joins", "remove delete merge", "Remove corners and merge the faces."},
    {"p5:Connect Verts##me", "Connect Verts", 5, "Edge loops and joins", "join cut", "Join two corners with an edge."},
    {"p5:Bridge Edge Loops##me", "Bridge Edge Loops", 5, "Edge loops and joins", "join connect", "Connect two edge loops with faces."},
    {"p5:Spin##me", "Spin", 5, "Edge loops and joins", "lathe revolve", "Spin the picked edges around an axis."},
    {"p5:Extrude Along Path##me", "Extrude Along Path", 5, "Edge loops and joins", "", ""},
    {"p5:Fill##me", "Fill", 5, "Edge loops and joins", "cap close", "Fill an opening with a face."},
    {"p5:Grid Fill##me", "Grid Fill", 5, "Edge loops and joins", "fill", "Fill an opening with a grid of faces."},
    {"p5:Merge at Center##me", "Merge at Center", 5, "Edge loops and joins", "", ""},
    {"p5:Merge at First##me", "Merge at First", 5, "Edge loops and joins", "", ""},
    {"p5:Separate##me", "Separate", 5, "Edge loops and joins", "split detach", "Move the picked parts into a new object."},
    {"p5:Mark Seam##uv", "Mark Seam", 5, "UVs & Seams", "uv cut", "Mark an edge as a UV cut."},
    {"p5:Clear Seam##uv", "Clear Seam", 5, "UVs & Seams", "uv", "Remove the UV cut."},
    {"p5:Clear All##uvseam", "Clear All", 5, "UVs & Seams", "", ""},
    {"p5:Seams from Angle##uv", "Seams from Angle", 5, "UVs & Seams", "", ""},
    {"p5:Unwrap##uv", "Unwrap", 5, "UVs & Seams", "uv texture flatten map", "Flatten the surface to a texture map (UV)."},
    {"p5:Smart Unwrap##uv", "Smart Unwrap", 5, "UVs & Seams", "uv texture auto", "Automatic UV layout."},
    {"p5:Pack Islands##uv", "Pack Islands", 5, "UVs & Seams", "uv texture arrange", "Arrange UV pieces to fill the map."},
    {"p5:UV Editor##uv", "UV Editor", 5, "UVs & Seams", "", ""},
    {"p5:Sculpt Brush (drag on mesh)##me", "Sculpt Brush (drag on mesh)", 5, "Sculpt", "", ""},
    {"p5:Paint Brush (drag on mesh)##me", "Paint Brush (drag on mesh)", 5, "Paint", "", ""},
    {"p5:Vertex colours (smooth)##paint", "Vertex colours (smooth)", 5, "Paint", "", ""},
    {"p5:Fill Vertices##paint", "Fill Vertices", 5, "Paint", "", ""},
    {"p5:Smooth Colours##paint", "Smooth Colours", 5, "Paint", "", ""},
    {"p5:Clear Vertex Colours##paint", "Clear Vertex Colours", 5, "Paint", "", ""},
    {"p5:Fill Sel##paint", "Fill Sel", 5, "Paint", "", ""},
    {"p5:Clear Paint##paint", "Clear Paint", 5, "Paint", "", ""},
    {"p5:+ Slot##slot", "+ Slot", 5, "Material Slots", "", ""},
    {"p5:- Slot##slot", "- Slot", 5, "Material Slots", "", ""},
    {"p5:Up##slot", "Up", 5, "Material Slots", "", ""},
    {"p5:Down##slot", "Down", 5, "Material Slots", "", ""},
    {"p5:Assign Sel##slot", "Assign Sel", 5, "Material Slots", "", ""},
    {"p5:Select Slot##slot", "Select Slot", 5, "Material Slots", "", ""},
    {"p5:Clear Slots##slot", "Clear Slots", 5, "Material Slots", "", ""},
    {"p6:Load##modelobj", "Load", 6, "Mesh file", "", ""},
    {"p6:Add##refimg", "Add reference image", 6, "Reference images (blueprints)", "", ""},
    {"p6:flip##refflip", "flip", 6, "Reference images (blueprints)", "", ""},
    {"p6:Remove##refdel", "Remove", 6, "Reference images (blueprints)", "", ""},
    {"p6:Refresh##meshlib", "Refresh", 6, "Mesh library (project Assets)", "", ""},
    {"p6:Add", "Add library part", 6, "Mesh library (project Assets)", "", ""},
    {"p6:Add Copy to Scene##addobj", "Add Copy to Scene", 6, "Add to scene as object", "", ""},
    {"p6:Add Physics (Rigidbody + Box)##model", "Add Physics (Rigidbody + Box)", 6, "Add to scene as object", "physics gravity", "Make it fall and collide."},
    {"p6:Sculpt (drag in the 3D view)##model", "Sculpt (drag in the 3D view)", 6, "Terrain", "", ""},
    {"p6:Add Mesh Renderer##model", "Add Mesh Renderer", 6, "Terrain", "draw show", "Make the object visible."},
    {"p2:Subdivide##model", "Subdivide", 2, "Detail", "add detail split faces", "Split every face into four."},
    {"p2:Subdiv + Smooth##model", "Subdiv + Smooth", 2, "Detail", "add detail smooth", "Split the faces, then relax them."},
    {"p2:Smooth##model", "Smooth", 2, "Detail", "round soften sphere", "Subdivide and push onto a sphere."},
    {"p2:Weld##model", "Weld", 2, "Pivot & export", "merge join duplicate", "Merge corners that sit at the same point."},
    {"p2:Recenter##model", "Recenter", 2, "Pivot & export", "pivot center origin", "Put the pivot in the middle."},
    {"p2:Ground##model", "Ground", 2, "Pivot & export", "pivot floor base", "Put the pivot at the bottom."},
    {"p2:Fit 1u##model", "Fit 1u", 2, "Pivot & export", "size scale unit", "Scale so the biggest side is 1."},
    {"p5:Save as Mesh Asset##meshasset", "Save as Mesh Asset", 5, "Pick parts", "save asset file okaymesh", "Save the model as a reusable file."},
    {"p6:Save as Mesh Asset##meshasset2", "Save as Mesh Asset", 6, "Mesh file", "save asset file okaymesh", "Save the model as a reusable file."},
    {"p0:Add shapes##mdlstart", "Add shapes", 0, "Start", "start begin no model empty", "Open the shapes page to make your first model."},
    {"p0:add_Cube", "Cube", 0, "Primitives", "add create new shape cube", "Add a cube to the scene."},
    {"p0:add_Sphere", "Sphere", 0, "Primitives", "add create new shape sphere", "Add a sphere to the scene."},
    {"p0:add_Cylinder", "Cylinder", 0, "Primitives", "add create new shape cylinder", "Add a cylinder to the scene."},
    {"p0:add_Cone", "Cone", 0, "Primitives", "add create new shape cone", "Add a cone to the scene."},
    {"p0:add_Pyramid", "Pyramid", 0, "Primitives", "add create new shape pyramid", "Add a pyramid to the scene."},
    {"p0:add_Wedge", "Wedge", 0, "Primitives", "add create new shape wedge", "Add a wedge to the scene."},
    {"p0:add_Plane", "Plane", 0, "Primitives", "add create new shape plane", "Add a plane to the scene."},
    {"p0:add_Tube", "Tube", 0, "Primitives", "add create new shape tube", "Add a tube to the scene."},
    {"p0:add_Torus", "Torus", 0, "Primitives", "add create new shape torus", "Add a torus to the scene."},
    {"p0:add_Capsule", "Capsule", 0, "Primitives", "add create new shape capsule", "Add a capsule to the scene."},
    {"p0:add_Icosphere", "Icosphere", 0, "Primitives", "add create new shape icosphere", "Add a icosphere to the scene."},
    {"p0:add_Stairs", "Stairs", 0, "Primitives", "add create new shape stairs", "Add a stairs to the scene."},
    {"p0:add_Gear", "Gear", 0, "Primitives", "add create new shape gear", "Add a gear to the scene."},
    {"p0:add_Tree", "Tree", 0, "Nature", "add create new shape tree", "Add a tree to the scene."},
    {"p0:add_Pine", "Pine", 0, "Nature", "add create new shape pine", "Add a pine to the scene."},
    {"p0:add_Rock", "Rock", 0, "Nature", "add create new shape rock", "Add a rock to the scene."},
    {"p0:add_Bush", "Bush", 0, "Nature", "add create new shape bush", "Add a bush to the scene."},
    {"p0:add_Crystal", "Crystal", 0, "Nature", "add create new shape crystal", "Add a crystal to the scene."},
    {"p0:add_Barrel", "Barrel", 0, "Props", "add create new shape barrel", "Add a barrel to the scene."},
    {"p0:add_Crate", "Crate", 0, "Props", "add create new shape crate", "Add a crate to the scene."},
    {"p0:add_Fence", "Fence", 0, "Props", "add create new shape fence", "Add a fence to the scene."},
    {"p0:add_House", "House", 0, "Structures", "add create new shape house", "Add a house to the scene."},
    {"p0:add_Tower", "Tower", 0, "Structures", "add create new shape tower", "Add a tower to the scene."},
    {"p0:add_Bridge", "Bridge", 0, "Structures", "add create new shape bridge", "Add a bridge to the scene."},
    {"p7:guided", "Guided designs", 7, "Guided", "template fence wall stairs door window roof building vehicle character", "Make fences, buildings, vehicles and characters from designs."},
    };
    return t;
}

inline std::string Lower(const std::string& s) { std::string r = s; for (char& c : r) c = (char)std::tolower((unsigned char)c); return r; }
inline std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> w; std::string cur;
    for (char c : s) { if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c); else if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
    if (!cur.empty()) w.push_back(cur);
    return w;
}
/// How well `t` matches `query` (every query word must start a word of the name, an alias or the section/page text): 0 = no match,
/// higher is better (a name hit beats an alias hit beats a section hit).
inline int Score(const Tool& t, const std::string& query) {
    const std::vector<std::string> q = Words(query);
    if (q.empty()) return 0;
    const std::vector<std::string> nm = Words(t.name), al = Words(t.aliases), sec = Words(std::string(t.section) + " " + (FindPage(t.page) ? std::string(FindPage(t.page)->name) + " " + FindPage(t.page)->was : std::string()));
    auto starts = [](const std::vector<std::string>& hay, const std::string& w) { for (const std::string& h : hay) if (h.rfind(w, 0) == 0) return true; return false; };
    int total = 0;
    for (const std::string& w : q) {
        if (starts(nm, w)) total += 10; else if (starts(al, w)) total += 5; else if (starts(sec, w)) total += 2; else return 0;
    }
    if (Lower(t.name) == Lower(query)) total += 20;
    return total;
}
struct Hit { const Tool* tool; int score; };
/// The best matches, best first (ties keep the sidebar order). Cost: one pass over about 200 tools, only while the box has text.
inline std::vector<Hit> Search(const std::string& query, std::size_t max = 12) {
    std::vector<Hit> hits;
    for (const Tool& t : Tools()) if (int s = Score(t, query)) hits.push_back({&t, s});
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (hits.size() > max) hits.resize(max);
    return hits;
}
/// The tool behind an ImGui button label ("Bevel##me") on a page, or null (labels are matched up to the "##").
inline const Tool* FindByLabel(int page, std::string imguiLabel) {
    if (imguiLabel.rfind("##", 0) == 0) imguiLabel.erase(0, 2);   // the Add page's tiles are "##add_Cube"
    for (const Tool& t : Tools()) {
        if (t.page != page) continue;
        const std::string id = t.id;
        const std::size_t colon = id.find(':');
        if (colon != std::string::npos && id.compare(colon + 1, std::string::npos, imguiLabel) == 0) return &t;
    }
    return nullptr;
}
/// True when the button with this label on this page should be highlighted for `query`.
inline bool HighlightLabel(int page, const std::string& imguiLabel, const std::string& query) {
    if (query.empty()) return false;
    const Tool* t = FindByLabel(page, imguiLabel);
    return t && Score(*t, query) > 0;
}

} // namespace oked::modeltools
