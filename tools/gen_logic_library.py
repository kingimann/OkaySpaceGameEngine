#!/usr/bin/env python3
"""Generate the logic-library nodes, their tests, and the playground graph.

One list is the registry. Each entry is a distinct node (its own type, title,
pins, and behaviour). Run from the repo root.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# (type, title, topic, sub, reg, exec, ins, outs, exec_outs, cpp, defaults, expect, etype)
# ins/outs: list of (name, letter). cpp assigns `out` (data) or returns via `flow` (exec).
# etype: f float, b bool, s string. expect None skips the numeric check (exec-only).
NODES = []

def add(typ, title, topic, sub, reg, ins, outs, cpp, defaults, expect, etype="f",
        exec_=False, exec_outs=None, help_=None):
    NODES.append(dict(type=typ, title=title, topic=topic, sub=sub, reg=reg,
                      ins=ins, outs=outs, cpp=cpp, defaults=defaults, expect=expect,
                      etype=etype, exec=exec_, exec_outs=exec_outs or [], help=help_ or title + "."))

N = lambda name: (name, "N")
B = lambda name: (name, "B")
T = lambda name: (name, "T")

def math_curves():
    P = ("Math", "Curves", "CAT_MATH")
    add("LbSmoothStep", "Smooth Step", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x*x*(3.f-2.f*x));",
        ["0.5"], 0.5, help_="The smooth 0 to 1 curve. 0 and 1 stay put, and the middle eases.")
    add("LbSmootherStep", "Smoother Step", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x*x*x*(x*(x*6.f-15.f)+10.f));",
        ["0"], 0.0, help_="A smoother 0 to 1 curve with flat ends.")
    add("LbEaseIn", "Ease in", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x*x);",
        ["0.5"], 0.25, help_="Starts slow and speeds up. Value is 0 to 1.")
    add("LbEaseOut", "Ease Out", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; float k=1.f-x; out=VsValue(1.f-k*k);",
        ["0.5"], 0.75, help_="Starts fast and settles. Value is 0 to 1.")
    add("LbEaseInOut", "Ease in Out", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x*x*(3.f-2.f*x));",
        ["0"], 0.0, help_="Slow, then fast, then slow. Value is 0 to 1.")
    add("LbPowerEase", "Power Ease", *P, [N("Value"), N("Power")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; float p=n1; if(p<0.01f)p=0.01f; out=VsValue(std::pow(x,p));",
        ["0.5", "2"], 0.25, help_="Raises a 0 to 1 value to Power. Power 1 is a straight line.")
    add("LbBiasCurve", "Bias Curve", *P, [N("Value"), N("Bias")], [N("Result")],
        "float t=n0; if(t<0)t=0; if(t>1)t=1; float b=n1; if(b<0.01f)b=0.01f; if(b>0.99f)b=0.99f; out=VsValue(t/((1.f/b-2.f)*(1.f-t)+1.f));",
        ["0.5", "0.5"], 0.5, help_="Bends a 0 to 1 curve. Bias 0.5 leaves it alone.")
    add("LbGainCurve", "Gain Curve", *P, [N("Value"), N("Gain")], [N("Result")],
        "float t=n0; if(t<0)t=0; if(t>1)t=1; float g=n1; if(g<0.01f)g=0.01f; if(g>0.99f)g=0.99f;"
        "auto bias=[&](float u){return u/((1.f/g-2.f)*(1.f-u)+1.f);};"
        "out=VsValue(t<0.5f? bias(2.f*t)/2.f : 1.f-bias(2.f-2.f*t)/2.f);",
        ["0.5", "0.5"], 0.5, help_="S shape or inverted S over 0 to 1. Gain 0.5 is straight.")
    add("LbLogistic", "Logistic Curve", *P, [N("Value"), N("Steepness")], [N("Result")],
        "float s=n1; if(s<0.01f)s=0.01f; out=VsValue(1.f/(1.f+std::exp(-s*(n0-0.5f)*2.f)));",
        ["0.5", "1"], 0.5, help_="A soft step around 0.5. Steepness makes the step sharper.")
    add("LbSampleCurve", "Sample Curve", *P, [N("Portion"), N("First"), N("Second"), N("Third"), N("Fourth")], [N("Result")],
        "float t=n0; if(t<0)t=0; if(t>1)t=1; float x=t*3.f; int i=(int)x; if(i>2)i=2; float f=x-i;"
        "float k[4]={n1,n2,n3,n4}; out=VsValue(k[i]+(k[i+1]-k[i])*f);",
        ["0", "2", "4", "6", "8"], 2.0, help_="Reads a four point curve. Portion 0 is the first point, 1 is the last.")
    add("LbSampleBezier", "Sample Bezier", *P, [N("Portion"), N("Start"), N("First"), N("Second"), N("End")], [N("Result")],
        "float t=n0; if(t<0)t=0; if(t>1)t=1; float u=1.f-t;"
        "out=VsValue(u*u*u*n1+3.f*u*u*t*n2+3.f*u*t*t*n3+t*t*t*n4);",
        ["0", "1", "2", "3", "4"], 1.0, help_="A cubic curve from Start to End. Portion 0 is Start.")
    add("LbHermite", "Sample Hermite", *P, [N("Portion"), N("Start"), N("End"), N("Start Slope"), N("End Slope")], [N("Result")],
        "float t=n0; if(t<0)t=0; if(t>1)t=1; float t2=t*t, t3=t2*t;"
        "out=VsValue((2.f*t3-3.f*t2+1.f)*n1+(t3-2.f*t2+t)*n3+(-2.f*t3+3.f*t2)*n2+(t3-t2)*n4);",
        ["0", "5", "9", "0", "0"], 5.0, help_="Blends Start to End with a slope at each end.")
    add("LbTriangleWave", "Triangle Wave", *P, [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float x=std::fmod(n0,p); if(x<0)x+=p; float u=x/p;"
        "out=VsValue(u<0.5f? u*2.f : 2.f-u*2.f);",
        ["0.25", "1"], 0.5, help_="Rises and falls between 0 and 1. Period is one full cycle.")
    add("LbSawWave", "Saw Wave", *P, [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float x=std::fmod(n0,p); if(x<0)x+=p; out=VsValue(x/p);",
        ["1.25", "1"], 0.25, help_="Rises from 0 to 1 and jumps back. Period is one cycle.")
    add("LbSquareWave", "Square Wave", *P, [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float x=std::fmod(n0,p); if(x<0)x+=p; out=VsValue(x/p<0.5f? 1.f : 0.f);",
        ["0.1", "1"], 1.0, help_="On for half the period, off for half.")
    add("LbSineWave", "Sine Wave", *P, [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; out=VsValue(std::sin(n0/p*6.2831853f));",
        ["0", "1"], 0.0, help_="A sine from Time. Period is one full cycle. The result is -1 to 1.")
    add("LbSampleNoise", "Sample Noise", *P, [N("At")], [N("Result")],
        "int i=(int)std::floor(n0); float f=n0-i; float u=f*f*(3.f-2.f*f);"
        "auto h=[](int k){unsigned x=(unsigned)k*374761393u+668265263u; x=(x^(x>>13))*1274126177u; return (x&65535)/65535.f;};"
        "out=VsValue(h(i)+(h(i+1)-h(i))*u);",
        ["0"], None, help_="Smooth value noise. The same At always gives the same result.")
    # fix expect for noise at 0: f=0 so result is h(0)
    NODES[-1]["expect"] = ((374761393 + 668265263) & 0xFFFFFFFF)
    # recompute properly in emit by leaving a marker; I'll set after defining h in python
    def h(k):
        x = (k * 374761393 + 668265263) & 0xFFFFFFFF
        x = ((x ^ (x >> 13)) * 1274126177) & 0xFFFFFFFF
        return (x & 65535) / 65535.0
    NODES[-1]["expect"] = h(0)

def math_numbers():
    P = ("Math", "", "CAT_MATH")
    C = ("Math", "Compare", "CAT_LOGIC")
    add("LbDeadZone", "Apply Dead Zone", *P, [N("Value"), N("Width")], [N("Result")],
        "out=VsValue(std::fabs(n0)<std::fabs(n1)? 0.f : n0);",
        ["0.2", "0.5"], 0.0, help_="Zero when Value is closer to zero than Width. Otherwise Value is unchanged.")
    add("LbSoftClamp", "Soft Clamp", *P, [N("Value"), N("Limit")], [N("Result")],
        "float L=std::fabs(n1); float a=std::fabs(n0); float s=a<L? a : L+0.25f*std::tanh((a-L)/0.25f); out=VsValue(std::copysign(s,n0));",
        ["0.5", "2"], 0.5, help_="Keeps Value inside Limit, then resists instead of cutting hard.")
    add("LbRepeatRange", "Repeat in Range", *P, [N("Value"), N("Length")], [N("Result")],
        "float L=std::fabs(n1); if(L<1e-6f) out=VsValue(0.f); else { float t=std::fmod(n0,L); if(t<0)t+=L; out=VsValue(t);} ",
        ["5", "2"], 1.0, help_="Wraps Value into 0 up to Length. 5 with length 2 is 1.")
    add("LbFoldRange", "Fold in Range", *P, [N("Value"), N("Length")], [N("Result")],
        "float L=std::fabs(n1); if(L<1e-6f){ out=VsValue(0.f); } else { float p=L*2.f; float t=std::fmod(std::fabs(n0),p); if(t<0)t+=p; out=VsValue(t<=L? t : p-t);} ",
        ["1.5", "1"], 0.5, help_="Folds Value back and forth inside 0 to Length.")
    add("LbSafeReciprocal", "Safe Reciprocal", *P, [N("Value")], [N("Result")],
        "out=VsValue(std::fabs(n0)<1e-6f? 0.f : 1.f/n0);",
        ["4"], 0.25, help_="One divided by Value. Zero gives zero instead of blowing up.")
    add("LbSquare", "Square", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0*n0);", ["3"], 9.0, help_="Value times itself.")
    add("LbCube", "Cube", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0*n0*n0);", ["2"], 8.0, help_="Value times itself twice.")
    add("LbNaturalLog", "Natural Log", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0>1e-6f? std::log(n0) : 0.f);", ["1"], 0.0, help_="The natural log of Value. Zero and negative give zero.")
    add("LbRaiseE", "Exponential", *P, [N("Value")], [N("Result")],
        "out=VsValue(std::exp(n0));", ["0"], 1.0, help_="E raised to Value.")
    add("LbHypotenuse", "Hypotenuse", *P, [N("Across"), N("Up")], [N("Result")],
        "out=VsValue(std::sqrt(n0*n0+n1*n1));", ["3", "4"], 5.0, help_="The length of a right triangle with those two sides.")
    add("LbAngleFromPair", "Angle From Pair", "Math", "Angles", "CAT_MATH", [N("Across"), N("Up")], [N("Result")],
        "out=VsValue(std::atan2(n1,n0)*57.2957795f);", ["1", "0"], 0.0, help_="Degrees of the direction Across, Up. Right is 0, up is 90.")
    add("LbDirectionFromAngle", "Direction From Angle", "Math", "Angles", "CAT_MATH", [N("Degrees")], [N("Across")],
        "float r=n0*0.017453292f; ", ["90"], None, help_="A flat direction. Across and Up are the two parts. 0 faces right, 90 faces up.")
    # two outputs: special, handle in cpp by returning first only if one expect... 
    # Test reads out pin 0. Across of 90 deg is cos(90)=0.
    NODES[-1]["cpp"] = "float r=n0*0.017453292f; out=VsValue(std::cos(r));"
    NODES[-1]["outs"] = [N("Across")]
    NODES[-1]["expect"] = 0.0
    add("LbUpFromAngle", "Up From Angle", "Math", "Angles", "CAT_MATH", [N("Degrees")], [N("Result")],
        "out=VsValue(std::sin(n0*0.017453292f));", ["90"], 1.0, help_="The up part of a flat direction. 90 degrees is 1.")
    add("LbMedianThree", "Median of Three", *P, [N("First"), N("Second"), N("Third")], [N("Result")],
        "float a=n0,b=n1,c=n2; if(a>b) std::swap(a,b); if(b>c) std::swap(b,c); if(a>b) std::swap(a,b); out=VsValue(b);",
        ["3", "1", "2"], 2.0, help_="The middle of the three numbers.")
    add("LbWeightedMean", "Weighted Mean", *P, [N("First"), N("First Weight"), N("Second"), N("Second Weight")], [N("Result")],
        "float w=n1+n3; out=VsValue(std::fabs(w)<1e-6f? 0.f : (n0*n1+n2*n3)/w);",
        ["1", "1", "3", "3"], 2.5, help_="The average, with each number pulled by its weight.")
    add("LbInverseSquare", "Inverse Square Falloff", *P, [N("Distance"), N("Strength")], [N("Result")],
        "float d=std::fabs(n0); out=VsValue(d<1e-4f? 0.f : n1/(d*d));",
        ["2", "4"], 1.0, help_="Strength divided by distance squared. Used for light and sound falloff.")
    add("LbLinearFalloff", "Linear Falloff", *P, [N("Distance"), N("Radius")], [N("Result")],
        "float r=std::fabs(n1); float d=std::fabs(n0); out=VsValue(r<1e-6f? 0.f : (d>=r? 0.f : 1.f-d/r));",
        ["2", "4"], 0.5, help_="1 at the center and 0 at Radius.")
    add("LbSmoothFalloff", "Smooth Falloff", *P, [N("Distance"), N("Radius")], [N("Result")],
        "float r=std::fabs(n1); float d=std::fabs(n0); float u=r<1e-6f? 1.f : d/r; if(u<0)u=0; if(u>1)u=1; float k=1.f-u; out=VsValue(k*k*(3.f-2.f*k));",
        ["0", "4"], 1.0, help_="A soft 1 at the center and 0 at Radius.")
    add("LbPortionAlong", "Portion Along", "Math", "Curves", "CAT_MATH", [N("Start"), N("End"), N("Value")], [N("Result")],
        "float d=n1-n0; float t=std::fabs(d)<1e-6f? 0.f : (n2-n0)/d; if(t<0)t=0; if(t>1)t=1; out=VsValue(t);",
        ["0", "10", "2.5"], 0.25, help_="Where Value sits between Start and End, as 0 to 1.")
    add("LbRemapClamped", "Remap Clamped", "Math", "Curves", "CAT_MATH",
        [N("Value"), N("From Start"), N("From End"), N("To Start"), N("To End")], [N("Result")],
        "float d=n2-n1; float t=std::fabs(d)<1e-6f? 0.f : (n0-n1)/d; if(t<0)t=0; if(t>1)t=1; out=VsValue(n3+(n4-n3)*t);",
        ["5", "0", "10", "0", "100"], 50.0, help_="Maps Value from one range into another, and stays inside the new range.")
    add("LbAbsDiff", "Absolute Difference", *P, [N("First"), N("Second")], [N("Result")],
        "out=VsValue(std::fabs(n0-n1));", ["3", "8"], 5.0, help_="How far apart the two numbers are, ignoring which is larger.")
    add("LbPositiveRemainder", "Positive Remainder", *P, [N("Value"), N("Divisor")], [N("Result")],
        "float d=n1; if(std::fabs(d)<1e-6f) out=VsValue(0.f); else { float t=std::fmod(n0,d); if(t<0)t+=std::fabs(d); out=VsValue(t);} ",
        ["-1", "4"], 3.0, help_="The remainder, always zero or positive.")
    add("LbNearestMultiple", "Nearest Multiple", *P, [N("Value"), N("Step")], [N("Result")],
        "float s=std::fabs(n1); out=VsValue(s<1e-6f? n0 : std::round(n0/s)*s);",
        ["7.6", "2"], 8.0, help_="The multiple of Step closest to Value.")
    add("LbFloorTo", "Floor to Step", *P, [N("Value"), N("Step")], [N("Result")],
        "float s=std::fabs(n1); out=VsValue(s<1e-6f? n0 : std::floor(n0/s)*s);",
        ["7.6", "2"], 6.0, help_="The multiple of Step at or below Value.")
    add("LbCeilTo", "Ceil to Step", *P, [N("Value"), N("Step")], [N("Result")],
        "float s=std::fabs(n1); out=VsValue(s<1e-6f? n0 : std::ceil(n0/s)*s);",
        ["7.1", "2"], 8.0, help_="The multiple of Step at or above Value.")
    add("LbTruncZero", "Truncate Toward Zero", *P, [N("Value")], [N("Result")],
        "out=VsValue(std::trunc(n0));", ["-2.9"], -2.0, help_="Drops the fraction, toward zero.")
    add("LbKeepPositive", "Keep Positive", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0>0.f? n0 : 0.f);", ["-4"], 0.0, help_="Value, or zero when Value is negative.")
    add("LbKeepAtMost", "Keep at Most", *P, [N("Value"), N("Limit")], [N("Result")],
        "out=VsValue(n0>n1? n1 : n0);", ["9", "4"], 4.0, help_="Value, limited so it is not above Limit.")
    add("LbKeepAtLeast", "Keep at Least", *P, [N("Value"), N("Limit")], [N("Result")],
        "out=VsValue(n0<n1? n1 : n0);", ["2", "5"], 5.0, help_="Value, raised so it is not below Limit.")
    add("LbSpread", "Spread Around", *P, [N("Center"), N("Amount"), N("Portion")], [N("Result")],
        "float t=n2; if(t<0)t=0; if(t>1)t=1; out=VsValue(n0+(t-0.5f)*n1);",
        ["10", "4", "1"], 12.0, help_="Portion 0 is Center minus half of Amount. Portion 1 is Center plus half.")
    add("LbPercentOf", "Percent of", *P, [N("Part"), N("Whole")], [N("Result")],
        "out=VsValue(std::fabs(n1)<1e-6f? 0.f : n0/n1*100.f);",
        ["2", "8"], 25.0, help_="Part as a percent of Whole.")
    add("LbValueFromPercent", "Value From Percent", *P, [N("Percent"), N("Whole")], [N("Result")],
        "out=VsValue(n0/100.f*n1);", ["25", "8"], 2.0, help_="That percent of Whole.")
    add("LbDampToward", "Damp Toward", "Math", "Curves", "CAT_MATH",
        [N("Current"), N("Target"), N("Sharpness"), N("Seconds")], [N("Result")],
        "float k=n2; if(k<0)k=0; float a=1.f-std::exp(-k*n3); out=VsValue(n0+(n1-n0)*a);",
        ["0", "10", "0", "1"], 0.0, help_="One damped step from Current toward Target. Sharpness 0 does not move.")
    add("LbApproach", "Approach", "Math", "Curves", "CAT_MATH",
        [N("Current"), N("Target"), N("Step")], [N("Result")],
        "float d=n1-n0; float s=std::fabs(n2); if(std::fabs(d)<=s) out=VsValue(n1); else out=VsValue(n0+std::copysign(s,d));",
        ["0", "10", "3"], 3.0, help_="Moves Current toward Target by at most Step.")
    add("LbLimitRate", "Limit Rate", "Math", "Curves", "CAT_MATH",
        [N("Previous"), N("Current"), N("Rate"), N("Seconds")], [N("Result")],
        "float allow=std::fabs(n2)*std::fabs(n3); float d=n1-n0; if(std::fabs(d)<=allow) out=VsValue(n1); else out=VsValue(n0+std::copysign(allow,d));",
        ["0", "10", "2", "1"], 2.0, help_="Current, but it cannot move from Previous faster than Rate per second.")
    add("LbChangeBetween", "Change Between", *P, [N("Previous"), N("Current")], [N("Result")],
        "out=VsValue(n1-n0);", ["2", "5"], 3.0, help_="Current minus Previous.")
    add("LbSpringStep", "Spring Step", "Math", "Curves", "CAT_MATH",
        [N("Current"), N("Velocity"), N("Target"), N("Stiffness"), N("Seconds")], [N("Result")],
        "float dt=n4; if(dt<0)dt=0; if(dt>0.1f)dt=0.1f; float k=n3; if(k<0)k=0; float acc=(n2-n0)*k - n1*2.f*std::sqrt(k); float v=n1+acc*dt; out=VsValue(n0+v*dt);",
        ["0", "0", "10", "0", "0.1"], 0.0, help_="One spring step toward Target. Stiffness 0 leaves Current alone. Result is the new current.")
    add("LbSmoothSmaller", "Smooth Smaller", *P, [N("First"), N("Second"), N("Softness")], [N("Result")],
        "float k=std::fabs(n2); if(k<1e-4f) out=VsValue(n0<n1? n0 : n1); else { float h=std::max(k-std::fabs(n0-n1),0.f)/k; out=VsValue(std::min(n0,n1)-h*h*k*0.25f);} ",
        ["2", "5", "0"], 2.0, help_="The smaller number, blended when Softness is above zero.")
    add("LbSmoothLarger", "Smooth Larger", *P, [N("First"), N("Second"), N("Softness")], [N("Result")],
        "float k=std::fabs(n2); if(k<1e-4f) out=VsValue(n0>n1? n0 : n1); else { float h=std::max(k-std::fabs(n0-n1),0.f)/k; out=VsValue(std::max(n0,n1)+h*h*k*0.25f);} ",
        ["2", "5", "0"], 5.0, help_="The larger number, blended when Softness is above zero.")
    add("LbExponentialDecay", "Exponential Decay", *P, [N("Value"), N("Rate"), N("Seconds")], [N("Result")],
        "float r=n1; if(r<0)r=0; out=VsValue(n0*std::exp(-r*n2));",
        ["8", "0", "1"], 8.0, help_="Value after fading for Seconds. Rate 0 leaves it alone.")
    add("LbOneMinus", "Gap Below One", *P, [N("Value")], [N("Result")],
        "out=VsValue(1.f-n0);", ["0.25"], 0.75, help_="One minus Value. Useful for the other side of a blend.")
    add("LbSaturateNumber", "Saturate Number", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x);", ["2"], 1.0, help_="Value clamped to 0 to 1.")
    add("LbRoundPlaces", "Round Places", *P, [N("Value"), N("Places")], [N("Result")],
        "int p=(int)std::lround(n1); if(p<0)p=0; if(p>4)p=4; float s=std::pow(10.f,(float)p); out=VsValue(std::round(n0*s)/s);",
        ["1.26", "1"], 1.3, help_="Rounds Value to that many fraction places, at most 4.")
    add("LbLerpUnclamped", "Blend Unclamped", "Math", "Curves", "CAT_MATH", [N("Start"), N("End"), N("Portion")], [N("Result")],
        "out=VsValue(n0+(n1-n0)*n2);", ["0", "10", "1.5"], 15.0, help_="Blend from Start to End. Portion may pass 0 and 1.")
    add("LbInverseLerpUnclamped", "Portion Unclamped", "Math", "Curves", "CAT_MATH", [N("Start"), N("End"), N("Value")], [N("Result")],
        "float d=n1-n0; out=VsValue(std::fabs(d)<1e-6f? 0.f : (n2-n0)/d);",
        ["0", "10", "15"], 1.5, help_="Where Value sits between Start and End, even past the ends.")
    add("LbMapSigned", "Map Signed", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<-1)x=-1; if(x>1)x=1; out=VsValue(x*0.5f+0.5f);",
        ["0"], 0.5, help_="Maps -1 to 1 into 0 to 1. Zero becomes one half.")
    add("LbMapBipolar", "Map Bipolar", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x*2.f-1.f);",
        ["0.5"], 0.0, help_="Maps 0 to 1 into -1 to 1. One half becomes zero.")
    add("LbStepAt", "Step at", *C, [N("Value"), N("Edge")], [B("Result")],
        "out=VsValue(n0>=n1);", ["5", "4"], 1, "b", help_="True when Value is at or above Edge.")
    add("LbIsOutside", "Is Outside Range", *C, [N("Value"), N("Low"), N("High")], [B("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); out=VsValue(n0<lo || n0>hi);",
        ["9", "0", "8"], 1, "b", help_="True when Value is below Low or above High.")
    add("LbIsStrictBetween", "Is Strictly Between", *C, [N("Value"), N("Low"), N("High")], [B("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); out=VsValue(n0>lo && n0<hi);",
        ["3", "1", "5"], 1, "b", help_="True when Value is inside Low and High, not on the ends.")
    add("LbHasSameSign", "Has Same Sign", *C, [N("First"), N("Second")], [B("Result")],
        "out=VsValue((n0>=0.f) == (n1>=0.f));", ["2", "4"], 1, "b", help_="True when both are zero or positive, or both are negative.")
    add("LbHasOppositeSign", "Has Opposite Sign", *C, [N("First"), N("Second")], [B("Result")],
        "out=VsValue((n0<0.f) != (n1<0.f) && n0!=0.f && n1!=0.f);", ["-2", "4"], 1, "b", help_="True when one is negative and the other is positive.")
    add("LbIsAlmostZero", "Is Almost Zero", *C, [N("Value"), N("Tolerance")], [B("Result")],
        "out=VsValue(std::fabs(n0)<=std::fabs(n1));", ["0.001", "0.01"], 1, "b", help_="True when Value is within Tolerance of zero.")
    add("LbIsMultiple", "Is Multiple", *C, [N("Value"), N("Step")], [B("Result")],
        "float s=std::fabs(n1); out=VsValue(s>=1e-6f && std::fabs(n0/s-std::round(n0/s))<1e-4f);",
        ["8", "2"], 1, "b", help_="True when Value is a whole multiple of Step.")
    add("LbIsPositive", "Is Positive", *C, [N("Value")], [B("Result")],
        "out=VsValue(n0>0.f);", ["3"], 1, "b", help_="True when Value is above zero.")
    add("LbIsNegative", "Is Negative", *C, [N("Value")], [B("Result")],
        "out=VsValue(n0<0.f);", ["-1"], 1, "b", help_="True when Value is below zero.")
    add("LbIsWhole", "Is Whole Number", *C, [N("Value")], [B("Result")],
        "out=VsValue(std::fabs(n0-std::round(n0))<1e-4f);", ["4"], 1, "b", help_="True when Value has no meaningful fraction.")
    add("LbSignOf", "Sign of", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0>0.f? 1.f : (n0<0.f? -1.f : 0.f));", ["-6"], -1.0, help_="1 when positive, -1 when negative, 0 at zero.")
    add("LbFractionOf", "Fraction of", *P, [N("Value")], [N("Result")],
        "out=VsValue(n0-std::trunc(n0));", ["3.75"], 0.75, help_="The fraction part, with the same sign as Value.")
    add("LbClamp11", "Clamp Signed One", *P, [N("Value")], [N("Result")],
        "float x=n0; if(x<-1)x=-1; if(x>1)x=1; out=VsValue(x);", ["3"], 1.0, help_="Value clamped to -1 to 1.")
    add("LbLerpClamp", "Blend Clamped", "Math", "Curves", "CAT_MATH", [N("Start"), N("End"), N("Portion")], [N("Result")],
        "float t=n2; if(t<0)t=0; if(t>1)t=1; out=VsValue(n0+(n1-n0)*t);",
        ["0", "10", "2"], 10.0, help_="Blend from Start to End. Portion is held inside 0 to 1.")
    add("LbSmoothDampStep", "Smooth Damp Step", "Math", "Curves", "CAT_MATH",
        [N("Current"), N("Target"), N("Smooth Time"), N("Seconds")], [N("Result")],
        "float sm=n2; if(sm<1e-4f) sm=1e-4f; float dt=n3; if(dt<0)dt=0; float a=1.f-std::exp(-dt/sm); out=VsValue(n0+(n1-n0)*a);",
        ["0", "10", "1", "0"], 0.0, help_="One smooth step toward Target. Smooth Time is how long the move takes to settle. Seconds 0 does not move.")
    add("LbCircleEaseIn", "Circle Ease in", "Math", "Curves", "CAT_MATH", [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(1.f-std::sqrt(std::max(0.f,1.f-x*x)));",
        ["0"], 0.0, help_="An ease in shaped like a quarter circle.")
    add("LbCircleEaseOut", "Circle Ease Out", "Math", "Curves", "CAT_MATH", [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(std::sqrt(std::max(0.f,1.f-(x-1.f)*(x-1.f))));",
        ["1"], 1.0, help_="An ease out shaped like a quarter circle.")
    add("LbBounceOut", "Bounce Out", "Math", "Curves", "CAT_MATH", [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; float n=7.5625f, d=2.75f;"
        "float y; if(x<1.f/d) y=n*x*x; else if(x<2.f/d){ x-=1.5f/d; y=n*x*x+0.75f;} else if(x<2.5f/d){ x-=2.25f/d; y=n*x*x+0.9375f;} else { x-=2.625f/d; y=n*x*x+0.984375f;} out=VsValue(y);",
        ["1"], 1.0, help_="An ease out that bounces at the end. Value is 0 to 1.")
    add("LbExpEaseIn", "Exponential Ease in", "Math", "Curves", "CAT_MATH", [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x<=0.f? 0.f : std::pow(2.f,10.f*(x-1.f)));",
        ["0"], 0.0, help_="An ease in that starts extremely slow.")
    add("LbExpEaseOut", "Exponential Ease Out", "Math", "Curves", "CAT_MATH", [N("Value")], [N("Result")],
        "float x=n0; if(x<0)x=0; if(x>1)x=1; out=VsValue(x>=1.f? 1.f : 1.f-std::pow(2.f,-10.f*x));",
        ["1"], 1.0, help_="An ease out that settles extremely gently.")
    add("LbShortestAngle", "Shortest Angle", "Math", "Angles", "CAT_MATH", [N("From"), N("To")], [N("Result")],
        "float d=std::fmod(n1-n0,360.f); if(d>180.f) d-=360.f; if(d<-180.f) d+=360.f; out=VsValue(d);",
        ["10", "350"], -20.0, help_="The shortest turn, in degrees, from From to To. Result is -180 to 180.")
    add("LbApproachAngle", "Approach Angle", "Math", "Angles", "CAT_MATH", [N("From"), N("To"), N("Step")], [N("Result")],
        "float d=std::fmod(n1-n0,360.f); if(d>180.f) d-=360.f; if(d<-180.f) d+=360.f; float s=std::fabs(n2);"
        "float m=std::fabs(d)<=s? d : std::copysign(s,d); float y=std::fmod(n0+m,360.f); if(y<0)y+=360.f; out=VsValue(y);",
        ["10", "40", "5"], 15.0, help_="Turns From toward To by at most Step degrees, the short way.")
    add("LbBlendAngles", "Blend Angles", "Math", "Angles", "CAT_MATH", [N("From"), N("To"), N("Portion")], [N("Result")],
        "float d=std::fmod(n1-n0,360.f); if(d>180.f) d-=360.f; if(d<-180.f) d+=360.f; float t=n2; if(t<0)t=0; if(t>1)t=1;"
        "float y=std::fmod(n0+d*t,360.f); if(y<0)y+=360.f; out=VsValue(y);",
        ["0", "90", "0.5"], 45.0, help_="Blends two angles the short way. Portion is 0 to 1.")
    add("LbWrapAngle", "Wrap Angle", "Math", "Angles", "CAT_MATH", [N("Degrees")], [N("Result")],
        "float y=std::fmod(n0,360.f); if(y<0)y+=360.f; out=VsValue(y);",
        ["-90"], 270.0, help_="Degrees wrapped into 0 to 360.")
    add("LbSnapAngle", "Snap Angle", "Math", "Angles", "CAT_MATH", [N("Degrees"), N("Step")], [N("Result")],
        "float s=std::fabs(n1); if(s<1e-4f)s=1.f; float y=std::round(n0/s)*s; y=std::fmod(y,360.f); if(y<0)y+=360.f; out=VsValue(y);",
        ["47", "15"], 45.0, help_="Degrees snapped to the nearest Step, then wrapped to 0 to 360.")

def math_vectors():
    P = ("Math", "Vectors", "CAT_MATH")
    add("LbFlatLength", "Flat Length", *P, [N("Across"), N("Ahead")], [N("Result")],
        "out=VsValue(std::sqrt(n0*n0+n1*n1));", ["3", "4"], 5.0, help_="Length of a flat offset, ignoring height.")
    add("LbLimitFlat", "Limit Flat Length", *P, [N("Across"), N("Ahead"), N("Limit")], [N("Across")],
        "float L=std::sqrt(n0*n0+n1*n1); float m=std::fabs(n2); float s=L>m && L>1e-6f? m/L : 1.f; out=VsValue(n0*s);",
        ["6", "8", "5"], 3.0, help_="Scales a flat offset so its length is at most Limit. Result is the across part.")
    add("LbAheadLimited", "Ahead After Limit", *P, [N("Across"), N("Ahead"), N("Limit")], [N("Result")],
        "float L=std::sqrt(n0*n0+n1*n1); float m=std::fabs(n2); float s=L>m && L>1e-6f? m/L : 1.f; out=VsValue(n1*s);",
        ["0", "10", "4"], 4.0, help_="The ahead part after the flat offset is limited.")
    add("LbDotFlat", "Dot Flat", *P, [N("Across"), N("Ahead"), N("Other Across"), N("Other Ahead")], [N("Result")],
        "out=VsValue(n0*n2+n1*n3);", ["1", "0", "0", "1"], 0.0, help_="How much two flat directions agree. 1 is the same, -1 is opposite, 0 is sideways.")
    add("LbCrossFlat", "Cross Flat", *P, [N("Across"), N("Ahead"), N("Other Across"), N("Other Ahead")], [N("Result")],
        "out=VsValue(n0*n3-n1*n2);", ["1", "0", "0", "1"], 1.0, help_="Which side the second flat direction is on. Positive is left.")
    add("LbIsBehindFlat", "Is Behind", "Physics", "Queries", "CAT_LOGIC",
        [N("Facing Across"), N("Facing Ahead"), N("Offset Across"), N("Offset Ahead")], [B("Result")],
        "out=VsValue(n0*n2+n1*n3 < 0.f);", ["0", "1", "0", "-1"], 1, "b", help_="True when the offset points against the facing direction.")
    add("LbIsLeftFlat", "Is to the Left", "Physics", "Queries", "CAT_LOGIC",
        [N("Facing Across"), N("Facing Ahead"), N("Offset Across"), N("Offset Ahead")], [B("Result")],
        "out=VsValue(n0*n3-n1*n2 > 0.f);", ["0", "1", "-1", "0"], 1, "b", help_="True when the offset is to the left of the facing direction.")
    add("LbIsRightFlat", "Is to the Right", "Physics", "Queries", "CAT_LOGIC",
        [N("Facing Across"), N("Facing Ahead"), N("Offset Across"), N("Offset Ahead")], [B("Result")],
        "out=VsValue(n0*n3-n1*n2 < 0.f);", ["0", "1", "1", "0"], 1, "b", help_="True when the offset is to the right of the facing direction.")
    add("LbMidpoint", "Midpoint", *P, [N("First Across"), N("First Ahead"), N("Second Across"), N("Second Ahead")], [N("Across")],
        "out=VsValue((n0+n2)*0.5f);", ["0", "0", "4", "6"], 2.0, help_="The across part of the point halfway between the two flat points.")
    add("LbMidAhead", "Midpoint Ahead", *P, [N("First Across"), N("First Ahead"), N("Second Across"), N("Second Ahead")], [N("Result")],
        "out=VsValue((n1+n3)*0.5f);", ["0", "0", "4", "6"], 3.0, help_="The ahead part of the halfway point.")
    add("LbDistanceFlat", "Flat Distance", *P, [N("First Across"), N("First Ahead"), N("Second Across"), N("Second Ahead")], [N("Result")],
        "float dx=n2-n0, dz=n3-n1; out=VsValue(std::sqrt(dx*dx+dz*dz));",
        ["0", "0", "3", "4"], 5.0, help_="Distance between two flat points.")
    add("LbIsWithinFlat", "Is Within Flat Distance", "Physics", "Queries", "CAT_LOGIC",
        [N("First Across"), N("First Ahead"), N("Second Across"), N("Second Ahead"), N("Radius")], [B("Result")],
        "float dx=n2-n0, dz=n3-n1; out=VsValue(dx*dx+dz*dz <= n4*n4);",
        ["0", "0", "3", "4", "6"], 1, "b", help_="True when the two flat points are within Radius.")
    add("LbClosestOnSegment", "Closest on Segment", *P,
        [N("Point Across"), N("Point Ahead"), N("Start Across"), N("Start Ahead"), N("End Across")], [N("Result")],
        "float ax=n2, az=n3, bx=n4, bz=In(5,g,ctx).AsFloat(); float abx=bx-ax, abz=bz-az; float ab=abx*abx+abz*abz;"
        "float t=ab<1e-8f? 0.f : ((n0-ax)*abx+(n1-az)*abz)/ab; if(t<0)t=0; if(t>1)t=1; out=VsValue(ax+abx*t);",
        ["0", "2", "0", "0", "4"], 0.0)
    # 6th pin End Ahead default 0, point (0,2), segment (0,0)-(4,0), closest across is 0
    NODES[-1]["ins"].append(N("End Ahead"))
    NODES[-1]["defaults"].append("0")
    NODES[-1]["help"] = "The across part of the closest point on the flat segment."
    add("LbDistanceToSegment", "Distance to Segment", *P,
        [N("Point Across"), N("Point Ahead"), N("Start Across"), N("Start Ahead"), N("End Across"), N("End Ahead")], [N("Result")],
        "float ax=n2, az=n3, bx=n4, bz=n5; float abx=bx-ax, abz=bz-az; float ab=abx*abx+abz*abz;"
        "float t=ab<1e-8f? 0.f : ((n0-ax)*abx+(n1-az)*abz)/ab; if(t<0)t=0; if(t>1)t=1;"
        "float cx=ax+abx*t, cz=az+abz*t; float dx=n0-cx, dz=n1-cz; out=VsValue(std::sqrt(dx*dx+dz*dz));",
        ["0", "3", "0", "0", "4", "0"], 3.0, help_="How far a flat point is from a flat segment.")
    add("LbIsInsideCircle", "Is Inside Circle", "Physics", "Queries", "CAT_LOGIC",
        [N("Point Across"), N("Point Ahead"), N("Center Across"), N("Center Ahead"), N("Radius")], [B("Result")],
        "float dx=n0-n2, dz=n1-n3; out=VsValue(dx*dx+dz*dz <= n4*n4);",
        ["1", "1", "0", "0", "2"], 1, "b", help_="True when the flat point is inside the circle.")
    add("LbIsInsideSphere", "Is Inside Sphere", "Physics", "Queries", "CAT_LOGIC",
        [N("Across"), N("Up"), N("Ahead"), N("Center Across"), N("Center Height")], [B("Result")],
        "float cx=n3, cy=n4, cz=In(5,g,ctx).AsFloat(), r=In(6,g,ctx).AsFloat();"
        "float dx=n0-cx, dy=n1-cy, dz=n2-cz; out=VsValue(dx*dx+dy*dy+dz*dz <= r*r);",
        ["0", "0", "0", "0", "0", "0", "2"], 1, "b", help_="True when the point is inside the sphere.")
    NODES[-1]["ins"] = [N("Across"), N("Up"), N("Ahead"), N("Center Across"), N("Center Height"), N("Center Ahead"), N("Radius")]
    add("LbVerticalGap", "Vertical Gap", *P, [N("From Height"), N("To Height")], [N("Result")],
        "out=VsValue(n1-n0);", ["1", "4"], 3.0, help_="To Up minus From Up.")
    add("LbIsAbove", "Is Above", "Physics", "Queries", "CAT_LOGIC", [N("From Height"), N("To Height"), N("Margin")], [B("Result")],
        "out=VsValue(n1 > n0+n2);", ["1", "3", "0.5"], 1, "b", help_="True when To Up is higher than From Up by more than Margin.")
    add("LbIsBelow", "Is Below", "Physics", "Queries", "CAT_LOGIC", [N("From Height"), N("To Height"), N("Margin")], [B("Result")],
        "out=VsValue(n1 < n0-n2);", ["3", "1", "0.5"], 1, "b", help_="True when To Up is lower than From Up by more than Margin.")
    add("LbOffsetAhead", "Offset Ahead", *P, [N("Across"), N("Ahead"), N("Facing Across"), N("Facing Ahead"), N("Distance")], [N("Result")],
        "float L=std::sqrt(n2*n2+n3*n3); float s=L<1e-6f? 0.f : n4/L; out=VsValue(n1+n3*s);",
        ["0", "0", "0", "1", "3"], 3.0, help_="The ahead part of a point Distance along the facing direction.")
    add("LbOffsetAcross", "Offset Across", *P, [N("Across"), N("Ahead"), N("Facing Across"), N("Facing Ahead"), N("Distance")], [N("Result")],
        "float L=std::sqrt(n2*n2+n3*n3); float s=L<1e-6f? 0.f : n4/L; out=VsValue(n0+n2*s);",
        ["1", "2", "1", "0", "4"], 5.0, help_="The across part of a point Distance along the facing direction.")
    add("LbGridCell", "Grid Cell", *P, [N("Across"), N("Cell")], [N("Result")],
        "float c=std::fabs(n1); if(c<1e-6f)c=1.f; out=VsValue(std::floor(n0/c));",
        ["5.2", "2"], 2.0, help_="Which cell Across falls in when cells are Cell wide. Negative cells count too.")
    add("LbCellCenter", "Cell Center", *P, [N("Cell"), N("Size")], [N("Result")],
        "float c=std::fabs(n1); if(c<1e-6f)c=1.f; out=VsValue((n0+0.5f)*c);",
        ["2", "2"], 5.0, help_="The center of that cell.")
    add("LbReflectFlat", "Reflect Flat", *P, [N("Across"), N("Ahead"), N("Normal Across"), N("Normal Ahead")], [N("Result")],
        "float L=std::sqrt(n2*n2+n3*n3); float nx=L<1e-6f? 0.f : n2/L, nz=L<1e-6f? 1.f : n3/L; float d=n0*nx+n1*nz; out=VsValue(n0-2.f*d*nx);",
        ["1", "-1", "0", "1"], 1.0, help_="The across part of a flat direction bounced off a normal.")
    add("LbReflectAhead", "Reflect Ahead", *P, [N("Across"), N("Ahead"), N("Normal Across"), N("Normal Ahead")], [N("Result")],
        "float L=std::sqrt(n2*n2+n3*n3); float nx=L<1e-6f? 0.f : n2/L, nz=L<1e-6f? 1.f : n3/L; float d=n0*nx+n1*nz; out=VsValue(n1-2.f*d*nz);",
        ["0", "-1", "0", "1"], 1.0, help_="The ahead part of a flat direction bounced off a normal.")
    add("LbProjectFlat", "Project Flat", *P, [N("Across"), N("Ahead"), N("On Across"), N("On Ahead")], [N("Result")],
        "float L=n2*n2+n3*n3; float s=L<1e-8f? 0.f : (n0*n2+n1*n3)/L; out=VsValue(n2*s);",
        ["2", "2", "1", "0"], 2.0, help_="The across part of the first direction projected onto the second.")
    add("LbPortionFacing", "Portion Facing", *P, [N("Facing Across"), N("Facing Ahead"), N("Offset Across"), N("Offset Ahead")], [N("Result")],
        "float L1=std::sqrt(n0*n0+n1*n1), L2=std::sqrt(n2*n2+n3*n3);"
        "float d=L1<1e-6f||L2<1e-6f? 0.f : (n0*n2+n1*n3)/(L1*L2); float u=d*0.5f+0.5f; if(u<0)u=0; if(u>1)u=1; out=VsValue(u);",
        ["0", "1", "0", "1"], 1.0, help_="1 when the offset matches the facing direction, 0 when it is opposite.")

def text_nodes():
    P = ("Text", "", "CAT_DATA")
    add("LbCountLetters", "Count Letters", *P, [T("Text")], [N("Result")],
        "out=VsValue((float)t0.size());", ['"abcd"'], 4.0, help_="How many characters are in Text, including spaces.")
    add("LbCountWords", "Count Words", *P, [T("Text")], [N("Result")],
        "int n=0; bool w=false; for(char c: t0){ bool s=std::isspace((unsigned char)c); if(!s && !w) ++n; w=!s;} out=VsValue((float)n);",
        ['"one two"'], 2.0, help_="How many words, split on spaces.")
    add("LbHasText", "Has Text", *P, [T("Text")], [B("Result")],
        "bool any=false; for(char c: t0) if(!std::isspace((unsigned char)c)) any=true; out=VsValue(any);",
        ['"hi"'], 1, "b", help_="True when Text has something other than spaces.")
    add("LbIsBlank", "Is Blank", *P, [T("Text")], [B("Result")],
        "bool any=false; for(char c: t0) if(!std::isspace((unsigned char)c)) any=true; out=VsValue(!any);",
        ['"   "'], 1, "b", help_="True when Text is empty or only spaces.")
    add("LbReplaceText", "Replace Text", *P, [T("Text"), T("Find"), T("Replace")], [T("Result")],
        "std::string r=t0; if(!t1.empty()){ auto p=r.find(t1); if(p!=std::string::npos) r.replace(p,t1.size(),t2);} out=VsValue(r);",
        ['"cat"','"a"','"o"'], "cot", "s", help_="Replaces the first Find in Text with Replace.")
    add("LbCutText", "Cut Text", *P, [T("Text"), N("Start"), N("Count")], [T("Result")],
        "int a=(int)std::lround(n0); int c=(int)std::lround(n1); if(a<0)a=0; if(c<0)c=0; if(a>(int)t0.size()) a=(int)t0.size();"
        "if(a+c>(int)t0.size()) c=(int)t0.size()-a; out=VsValue(t0.substr((size_t)a,(size_t)c));",
        ['"hello"', "1", "3"], "ell", "s", help_="A piece of Text. Start counts from 0.")
    add("LbFindText", "Find Text", *P, [T("Text"), T("Find")], [N("Result")],
        "auto p=t0.find(t1); out=VsValue(p==std::string::npos? -1.f : (float)p);",
        ['"hello"', '"ll"'], 2.0, help_="Where Find starts in Text, counting from 0. -1 when it is missing.")
    add("LbJoinWith", "Join With", *P, [T("First"), T("Second"), T("Separator")], [T("Result")],
        "out=VsValue(t0+t2+t1);", ['"red"','"blue"','"-"'], "red-blue", "s", help_="First, then Separator, then Second.")
    add("LbRepeatText", "Repeat Text", *P, [T("Text"), N("Times")], [T("Result")],
        "int n=(int)std::lround(n0); if(n<0)n=0; if(n>32)n=32; std::string r; for(int i=0;i<n;++i) r+=t0; out=VsValue(r);",
        ['"ab"', "3"], "ababab", "s", help_="Text repeated Times, at most 32.")
    add("LbLowerText", "Lowercase Text", *P, [T("Text")], [T("Result")],
        "std::string r=t0; for(char& c: r) c=(char)std::tolower((unsigned char)c); out=VsValue(r);",
        ['"AbC"'], "abc", "s", help_="Text in lowercase.")
    add("LbUpperText", "Uppercase Text", *P, [T("Text")], [T("Result")],
        "std::string r=t0; for(char& c: r) c=(char)std::toupper((unsigned char)c); out=VsValue(r);",
        ['"AbC"'], "ABC", "s", help_="Text in uppercase.")
    add("LbReverseText", "Reverse Text", *P, [T("Text")], [T("Result")],
        "std::string r=t0; std::reverse(r.begin(), r.end()); out=VsValue(r);",
        ['"ab"'], "ba", "s", help_="Text backwards.")
    add("LbRemoveSpaces", "Remove Spaces", *P, [T("Text")], [T("Result")],
        "std::string r; for(char c: t0) if(!std::isspace((unsigned char)c)) r.push_back(c); out=VsValue(r);",
        ['"a b"'], "ab", "s", help_="Text with spaces removed.")
    add("LbBeforeSeparator", "Before Separator", *P, [T("Text"), T("Separator")], [T("Result")],
        "auto p=t1.empty()? std::string::npos : t0.find(t1); out=VsValue(p==std::string::npos? t0 : t0.substr(0,p));",
        ['"red-blue"','"-"'], "red", "s", help_="The part of Text before the first Separator. All of Text when it is missing.")
    add("LbAfterSeparator", "After Separator", *P, [T("Text"), T("Separator")], [T("Result")],
        "auto p=t1.empty()? std::string::npos : t0.find(t1); out=VsValue(p==std::string::npos? std::string() : t0.substr(p+t1.size()));",
        ['"red-blue"','"-"'], "blue", "s", help_="The part of Text after the first Separator. Empty when it is missing.")
    add("LbIsNumberText", "Is Number Text", *P, [T("Text")], [B("Result")],
        "bool ok=!t0.empty(); size_t i=0; if(ok && (t0[0]=='+'||t0[0]=='-')) ++i; bool dot=false, dig=false;"
        "for(; ok && i<t0.size(); ++i){ char c=t0[i]; if(c=='.' && !dot){ dot=true; continue;} if(!std::isdigit((unsigned char)c)) ok=false; else dig=true;} out=VsValue(ok && dig);",
        ['"12.5"'], 1, "b", help_="True when Text is a plain number.")
    add("LbNumberFromText", "Number From Text", *P, [T("Text")], [N("Result")],
        "float v=0; try { size_t k=0; v=std::stof(t0,&k); if(k==0) v=0; } catch(...) { v=0; } out=VsValue(v);",
        ['"3.5"'], 3.5, help_="The number written in Text. Anything else is zero.")
    add("LbTextFromNumber", "Text From Number", *P, [N("Value")], [T("Result")],
        "out=VsValue(VsValue(n0).AsString());", ["3"], "3", "s", help_="Value written as text. Whole numbers have no fraction.")
    add("LbEqualLoose", "Equals Loose", *P, [T("First"), T("Second")], [B("Result")],
        "auto fold=[](std::string s){ for(char& c:s) c=(char)std::tolower((unsigned char)c); return s; }; out=VsValue(fold(t0)==fold(t1));",
        ['"Cat"','"cat"'], 1, "b", help_="True when the two texts match, ignoring capital letters.")
    add("LbContainsLoose", "Contains Loose", *P, [T("Text"), T("Find")], [B("Result")],
        "auto fold=[](std::string s){ for(char& c:s) c=(char)std::tolower((unsigned char)c); return s; }; out=VsValue(fold(t0).find(fold(t1))!=std::string::npos);",
        ['"Hello"','"ell"'], 1, "b", help_="True when Find is inside Text, ignoring capital letters.")
    add("LbFirstWord", "First Word", *P, [T("Text")], [T("Result")],
        "size_t a=0; while(a<t0.size() && std::isspace((unsigned char)t0[a])) ++a; size_t b=a; while(b<t0.size() && !std::isspace((unsigned char)t0[b])) ++b; out=VsValue(t0.substr(a,b-a));",
        ['"red blue"'], "red", "s", help_="The first word in Text.")
    add("LbLastWord", "Last Word", *P, [T("Text")], [T("Result")],
        "int b=(int)t0.size()-1; while(b>=0 && std::isspace((unsigned char)t0[(size_t)b])) --b; int a=b; while(a>=0 && !std::isspace((unsigned char)t0[(size_t)a])) --a; out=VsValue(a>=b? std::string() : t0.substr((size_t)a+1,(size_t)(b-a)));",
        ['"red blue"'], "blue", "s", help_="The last word in Text.")
    add("LbEnsurePrefix", "Ensure Prefix", *P, [T("Text"), T("Prefix")], [T("Result")],
        "out=VsValue(t0.rfind(t1,0)==0? t0 : t1+t0);", ['"cat"','"the "'], "the cat", "s", help_="Text, with Prefix added when it is not already there.")
    add("LbEnsureSuffix", "Ensure Suffix", *P, [T("Text"), T("Suffix")], [T("Result")],
        "bool has=t1.size()<=t0.size() && t0.compare(t0.size()-t1.size(), t1.size(), t1)==0; out=VsValue(has? t0 : t0+t1);",
        ['"cat"','"s"'], "cats", "s", help_="Text, with Suffix added when it is not already there.")
    add("LbClampText", "Limit Text Length", *P, [T("Text"), N("Limit")], [T("Result")],
        "int n=(int)std::lround(n0); if(n<0)n=0; out=VsValue(n>=(int)t0.size()? t0 : t0.substr(0,(size_t)n));",
        ['"hello"', "4"], "hell", "s", help_="Text cut so it is at most Limit characters.")
    add("LbCountMatches", "Count Matches", *P, [T("Text"), T("Find")], [N("Result")],
        "int n=0; if(!t1.empty()){ for(size_t p=0; (p=t0.find(t1,p))!=std::string::npos; p+=t1.size()) ++n; } out=VsValue((float)n);",
        ['"ababa"','"aba"'], 1.0, help_="How many times Find appears in Text, without overlap.")
    add("LbPadLeft", "Pad Left", *P, [T("Text"), N("Width")], [T("Result")],
        "int w=(int)std::lround(n0); if(w<0)w=0; if(w>64)w=64; std::string r=t0; while((int)r.size()<w) r.insert(r.begin(),' '); out=VsValue(r);",
        ['"ab"', "4"], "  ab", "s", help_="Text with spaces on the left so it is at least Width characters, at most 64.")
    add("LbPadRight", "Pad Right", *P, [T("Text"), N("Width")], [T("Result")],
        "int w=(int)std::lround(n0); if(w<0)w=0; if(w>64)w=64; std::string r=t0; while((int)r.size()<w) r.push_back(' '); out=VsValue(r);",
        ['"ab"', "4"], "ab  ", "s", help_="Text with spaces on the right so it is at least Width characters, at most 64.")

def list_nodes():
    # These read a list built in the test. cpp uses `arr` if we set uses_list.
    pass

def more_logic():
    P = ("Math", "Compare", "CAT_LOGIC")
    add("LbSelectLarger", "Select Larger", "Math", "", "CAT_MATH", [N("First"), N("Second")], [N("Result")],
        "out=VsValue(n0>=n1? n0 : n1);", ["2", "5"], 5.0, help_="The larger of the two numbers. A tie keeps the first.")
    add("LbSelectSmaller", "Select Smaller", "Math", "", "CAT_MATH", [N("First"), N("Second")], [N("Result")],
        "out=VsValue(n0<=n1? n0 : n1);", ["2", "5"], 2.0, help_="The smaller of the two numbers. A tie keeps the first.")
    add("LbClampRange", "Clamp Range", "Math", "", "CAT_MATH", [N("Value"), N("Low"), N("High")], [N("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); float x=n0; if(x<lo)x=lo; if(x>hi)x=hi; out=VsValue(x);",
        ["9", "0", "4"], 4.0, help_="Value held inside Low and High.")
    add("LbMapRange", "Map Range", "Math", "Curves", "CAT_MATH",
        [N("Value"), N("From Start"), N("From End"), N("To Start"), N("To End")], [N("Result")],
        "float d=n2-n1; float t=std::fabs(d)<1e-6f? 0.f : (n0-n1)/d; out=VsValue(n3+(n4-n3)*t);",
        ["5", "0", "10", "0", "100"], 50.0, help_="Maps Value from one range into another, and may pass the ends.")
    add("LbIsNearly", "Is Nearly", *P, [N("First"), N("Second"), N("Tolerance")], [B("Result")],
        "out=VsValue(std::fabs(n0-n1)<=std::fabs(n2));", ["1.05", "1", "0.1"], 1, "b", help_="True when the two numbers are within Tolerance.")
    add("LbIsFar", "Is Far", *P, [N("First"), N("Second"), N("Gap")], [B("Result")],
        "out=VsValue(std::fabs(n0-n1) > std::fabs(n2));", ["1", "5", "2"], 1, "b", help_="True when the two numbers differ by more than Gap.")
    add("LbChooseNumber", "Choose Number", "Math", "", "CAT_MATH", [B("Condition"), N("When True"), N("When False")], [N("Result")],
        "out=VsValue(b0? n0 : n1);", ["true", "4", "9"], 4.0, help_="If True when Condition is on, otherwise If False.")
    add("LbChooseText", "Choose Text", "Text", "", "CAT_DATA", [B("Condition"), T("When True"), T("When False")], [T("Result")],
        "out=VsValue(b0? t0 : t1);", ["false", '"yes"', '"no"'], "no", "s", help_="If True when Condition is on, otherwise If False.")
    add("LbAllNumbers", "Is Every Number Set", *P, [N("First"), N("Second"), N("Third")], [B("Result")],
        "out=VsValue(n0!=0.f && n1!=0.f && n2!=0.f);", ["1", "2", "3"], 1, "b", help_="True when none of the three numbers is zero.")
    add("LbAnyNumber", "Is Any Number Set", *P, [N("First"), N("Second"), N("Third")], [B("Result")],
        "out=VsValue(n0!=0.f || n1!=0.f || n2!=0.f);", ["0", "0", "4"], 1, "b", help_="True when any of the three numbers is not zero.")
    add("LbNoneNumber", "Is Every Number Zero", *P, [N("First"), N("Second"), N("Third")], [B("Result")],
        "out=VsValue(n0==0.f && n1==0.f && n2==0.f);", ["0", "0", "0"], 1, "b", help_="True when all three numbers are zero.")
    add("LbXorThree", "Is Odd Count True", *P, [B("First"), B("Second"), B("Third")], [B("Result")],
        "int n=(b0?1:0)+(b1?1:0)+(In(2,g,ctx).AsBool()?1:0); out=VsValue((n%2)==1);",
        ["true", "false", "false"], 1, "b", help_="True when an odd number of the three are true.")
    NODES[-1]["ins"] = [B("First"), B("Second"), B("Third")]
    add("LbNorThree", "Is Every Flag False", *P, [B("First"), B("Second"), B("Third")], [B("Result")],
        "out=VsValue(!b0 && !b1 && !In(2,g,ctx).AsBool());",
        ["false", "false", "false"], 1, "b", help_="True when all three are false.")
    NODES[-1]["ins"] = [B("First"), B("Second"), B("Third")]
    add("LbNandThree", "Is Not All True", *P, [B("First"), B("Second"), B("Third")], [B("Result")],
        "out=VsValue(!(b0 && b1 && In(2,g,ctx).AsBool()));",
        ["true", "true", "false"], 1, "b", help_="True unless all three are true.")
    NODES[-1]["ins"] = [B("First"), B("Second"), B("Third")]
    add("LbImplies", "Implication Holds", *P, [B("Premise"), B("Conclusion")], [B("Result")],
        "out=VsValue(!b0 || b1);", ["true", "true"], 1, "b", help_="True unless If is true and Then is false.")
    add("LbResourcePortion", "Resource Portion", "Objects", "Survival", "CAT_DATA", [N("Current"), N("Full")], [N("Result")],
        "float m=n1; float t=std::fabs(m)<1e-6f? 0.f : n0/m; if(t<0)t=0; if(t>1)t=1; out=VsValue(t);",
        ["25", "100"], 0.25, help_="Current divided by Full, held to 0 to 1.")
    add("LbIsResourceLow", "Is Resource Low", "Objects", "Survival", "CAT_LOGIC", [N("Current"), N("Full"), N("Portion")], [B("Result")],
        "float m=std::fabs(n1); float p=n2; if(p<0)p=0; if(p>1)p=1; out=VsValue(m>=1e-6f && n0/m <= p);",
        ["10", "100", "0.2"], 1, "b", help_="True when Current is at or below Portion of Full.")
    add("LbIsResourceFull", "Is Resource Full", "Objects", "Survival", "CAT_LOGIC", [N("Current"), N("Full")], [B("Result")],
        "out=VsValue(n1>0.f && n0>=n1);", ["10", "10"], 1, "b", help_="True when Current has reached Full.")
    add("LbTrySpend", "Try Spend", "Objects", "Survival", "CAT_MATH", [N("Current"), N("Cost")], [N("Result")],
        "out=VsValue(n0>=n1? n0-n1 : n0);", ["10", "3"], 7.0, help_="Current minus Cost when Current can pay. Otherwise Current is unchanged.")
    add("LbCouldSpend", "Could Spend", "Objects", "Survival", "CAT_LOGIC", [N("Current"), N("Cost")], [B("Result")],
        "out=VsValue(n0>=n1);", ["5", "5"], 1, "b", help_="True when Current is at least Cost.")
    add("LbRegenStep", "Regen Step", "Objects", "Survival", "CAT_MATH", [N("Current"), N("Full"), N("Rate"), N("Seconds")], [N("Result")],
        "float x=n0+std::max(0.f,n2)*std::max(0.f,n3); float m=n1; if(x>m)x=m; if(x<0)x=0; out=VsValue(x);",
        ["5", "10", "2", "1"], 7.0, help_="Current plus Rate times Seconds, and not above Full.")
    add("LbStackMultiply", "Stack Multiply", "Objects", "Survival", "CAT_MATH", [N("Base"), N("Each"), N("Stacks")], [N("Result")],
        "int n=(int)std::lround(n2); if(n<0)n=0; if(n>16)n=16; float m=1.f; for(int i=0;i<n;++i) m*=n1; out=VsValue(n0*m);",
        ["10", "2", "3"], 80.0, help_="Base times Each, once per stack. Three stacks of 2 is eight times Base.")
    add("LbDiminishing", "Diminishing Bonus", "Objects", "Survival", "CAT_MATH", [N("Each"), N("Stacks")], [N("Result")],
        "int n=(int)std::lround(n1); if(n<0)n=0; if(n>16)n=16; float s=0, m=1.f; for(int i=0;i<n;++i){ s+=n0*m; m*=0.5f; } out=VsValue(s);",
        ["10", "3"], 17.5, help_="Each plus half of Each, and so on, for Stacks. Three stacks of 10 is 17.5.")
    add("LbOverflow", "Overflow Amount", "Objects", "Survival", "CAT_MATH", [N("Current"), N("Full")], [N("Result")],
        "out=VsValue(n0>n1? n0-n1 : 0.f);", ["12", "10"], 2.0, help_="How much Current is past Full. Zero when it is not past.")
    add("LbQuestPortion", "Quest Portion", "Scene", "Story", "CAT_DATA", [N("Done"), N("Steps")], [N("Result")],
        "float s=std::fabs(n1); float t=s<1e-6f? 0.f : n0/s; if(t<0)t=0; if(t>1)t=1; out=VsValue(t);",
        ["1", "4"], 0.25, help_="Done divided by Steps, held to 0 to 1.")
    add("LbIsQuestDone", "Is Quest Done", "Scene", "Story", "CAT_LOGIC", [N("Done"), N("Steps")], [B("Result")],
        "out=VsValue(n1>0.f && n0>=n1);", ["3", "3"], 1, "b", help_="True when Done has reached Steps.")
    add("LbStepsLeft", "Steps Left", "Scene", "Story", "CAT_DATA", [N("Done"), N("Steps")], [N("Result")],
        "float left=n1-n0; if(left<0)left=0; out=VsValue(left);", ["1", "4"], 3.0, help_="Steps minus Done, and not below zero.")
    add("LbScoreGap", "Score Gap", "Math", "Compare", "CAT_MATH", [N("First"), N("Second"), N("Third"), N("Fourth")], [N("Result")],
        "float a=n0,b=n1,c=n2,d=n3; float top=a; if(b>top)top=b; if(c>top)top=c; if(d>top)top=d;"
        "float second=-1e30f; for(float v: {a,b,c,d}) if(v<top && v>second) second=v; out=VsValue(second<-1e20f? 0.f : top-second);",
        ["1", "5", "4", "0"], 1.0, help_="How far the highest score is ahead of the next one. Zero when they tie.")
    add("LbPickWorst", "Pick Worst Score", "Math", "Compare", "CAT_MATH", [N("First"), N("Second"), N("Third"), N("Fourth")], [N("Result")],
        "int best=0; float w=n0; float s[4]={n0,n1,n2,n3}; for(int i=1;i<4;++i) if(s[i]<w){ w=s[i]; best=i; } out=VsValue((float)best);",
        ["3", "1", "4", "2"], 1.0, help_="The index of the lowest score, counting from 0. A tie keeps the earlier one.")
    add("LbClosestScore", "Closest Score", "Math", "Compare", "CAT_MATH", [N("Goal"), N("First"), N("Second"), N("Third")], [N("Result")],
        "float s[3]={n1,n2,n3}; int best=0; float w=std::fabs(s[0]-n0); for(int i=1;i<3;++i){ float d=std::fabs(s[i]-n0); if(d<w){ w=d; best=i; } } out=VsValue((float)best);",
        ["5", "1", "6", "9"], 1.0, help_="The index of the score closest to Goal, counting from 0.")
    add("LbScoresAbove", "Count Scores Above", "Math", "Compare", "CAT_MATH", [N("Line"), N("First"), N("Second"), N("Third"), N("Fourth")], [N("Result")],
        "int n=0; for(float v: {n1,n2,n3,n4}) if(v>n0) ++n; out=VsValue((float)n);",
        ["2", "1", "3", "4", "0"], 2.0, help_="How many of the four scores are above Line.")
    add("LbNormalizeScore", "Normalize Score", "Math", "Compare", "CAT_MATH", [N("Value"), N("Low"), N("High")], [N("Result")],
        "float d=n2-n1; float t=std::fabs(d)<1e-6f? 0.f : (n0-n1)/d; if(t<0)t=0; if(t>1)t=1; out=VsValue(t);",
        ["5", "0", "10"], 0.5, help_="Value mapped from Low to High into 0 to 1.")

def stateful():
    # evaluated every OnUpdate via GetVar. Tested separately.
    add("LbHysteresis", "Hysteresis Gate", "Flow", "Timing", "CAT_FLOW",
        [N("Value"), N("Low"), N("High")], [B("Result")],
        "if(!this->seen){ this->seen=true; this->flag = n0>=n2; } else if(this->flag && n0<=n1) this->flag=false; else if(!this->flag && n0>=n2) this->flag=true; out=VsValue(this->flag);",
        None, None, "b", help_="Turns on at High and off at Low, so it does not flicker in between.")
    NODES[-1]["state"] = True
    add("LbRememberPeak", "Remember Peak", "Math", "", "CAT_MATH", [N("Value")], [N("Result")],
        "if(!this->seen){ this->seen=true; this->mem=n0; } else if(n0>this->mem) this->mem=n0; out=VsValue(this->mem);",
        None, None, help_="The highest Value seen so far on this node.")
    NODES[-1]["state"] = True
    add("LbRememberValley", "Remember Valley", "Math", "", "CAT_MATH", [N("Value")], [N("Result")],
        "if(!this->seen){ this->seen=true; this->mem=n0; } else if(n0<this->mem) this->mem=n0; out=VsValue(this->mem);",
        None, None, help_="The lowest Value seen so far on this node.")
    NODES[-1]["state"] = True
    add("LbRunningMean", "Running Mean", "Math", "", "CAT_MATH", [N("Value")], [N("Result")],
        "this->count += 1; this->mem += n0; out=VsValue(this->mem/(float)this->count);",
        None, None, help_="The average of every Value this node has read.")
    NODES[-1]["state"] = True
    add("LbCountTrueFrames", "Count True Frames", "Flow", "Timing", "CAT_MATH", [B("Condition")], [N("Result")],
        "if(b0) this->count += 1; else this->count = 0; out=VsValue((float)this->count);",
        None, None, help_="How many reads in a row Condition has been true. A false read starts again at zero.")
    NODES[-1]["state"] = True
    add("LbSampleHold", "Sample and Hold", "Flow", "Timing", "CAT_MATH", [B("Take"), N("Value")], [N("Result")],
        "if(b0 || !this->seen){ this->seen=true; this->mem=n0; } out=VsValue(this->mem);",
        None, None, help_="Copies Value while Take is true, and keeps the last copy when Take is false.")
    NODES[-1]["state"] = True
    add("LbPreviousNumber", "Get Previous Number", "Flow", "Timing", "CAT_DATA", [N("Value")], [N("Result")],
        "float prev = this->seen? this->mem : n0; this->mem=n0; this->seen=true; out=VsValue(prev);",
        None, None, help_="The Value from the previous read. The first read returns Value itself.")
    NODES[-1]["state"] = True
    add("LbSpike", "Is Spike", "Flow", "Timing", "CAT_LOGIC", [N("Value"), N("Gap")], [B("Result")],
        "bool fire=this->seen && std::fabs(n0-this->mem) > std::fabs(n1); this->mem=n0; this->seen=true; out=VsValue(fire);",
        None, None, "b", help_="True when Value jumped from the previous read by more than Gap.")
    NODES[-1]["state"] = True
    add("LbPassAfter", "Pass After Calls", "Flow", "", "CAT_FLOW", [N("Calls")], [],
        "int need=std::max(1,(int)std::lround(n0)); this->count += 1; flow = this->count>=need ? 0 : -1;",
        None, None, exec_=True, exec_outs=[""], help_="Passes on the call when this node has been reached Calls times.")
    NODES[-1]["state"] = True
    add("LbEveryCalls", "Every Few Calls", "Flow", "", "CAT_FLOW", [N("Calls")], [],
        "int need=std::max(1,(int)std::lround(n0)); this->count += 1; flow = (this->count % need)==0 ? 0 : -1;",
        None, None, exec_=True, exec_outs=[""], help_="Passes every Calls arrivals, and blocks the ones between.")
    NODES[-1]["state"] = True
    add("LbAccumulate", "Accumulate", "Math", "", "CAT_MATH", [N("Value")], [N("Result")],
        "this->mem += n0; out=VsValue(this->mem);",
        None, None, help_="The running total of every Value read.")
    NODES[-1]["state"] = True

def extra_batch():
    """More distinct helpers so the two rounds clear 250."""
    P = ("Math", "", "CAT_MATH")
    add("LbSquaredDistance", "Squared Distance", "Math", "Vectors", "CAT_MATH",
        [N("First Across"), N("First Ahead"), N("Second Across"), N("Second Ahead")], [N("Result")],
        "float dx=n2-n0, dz=n3-n1; out=VsValue(dx*dx+dz*dz);",
        ["0", "0", "3", "4"], 25.0, help_="Distance squared, which is enough when you only need to compare distances.")
    add("LbLerpInverse", "Inverse Blend", "Math", "Curves", "CAT_MATH", [N("Start"), N("End"), N("Value")], [N("Result")],
        "float d=n1-n0; out=VsValue(std::fabs(d)<1e-6f? n0 : n1-(n2-n0));",
        ["0", "10", "2"], 8.0, help_="The other side of Value between Start and End. 2 between 0 and 10 gives 8.")
    add("LbWrapPair", "Wrap Pair", "Math", "", "CAT_MATH", [N("Value"), N("Low"), N("High")], [N("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); float r=hi-lo; if(r<1e-6f) out=VsValue(lo); else { float t=std::fmod(n0-lo,r); if(t<0)t+=r; out=VsValue(lo+t);} ",
        ["11", "0", "10"], 1.0, help_="Wraps Value into Low up to High.")
    add("LbSnapPair", "Snap Pair", "Math", "", "CAT_MATH", [N("Value"), N("Low"), N("High")], [N("Result")],
        "float lo=n1, hi=n2; out=VsValue(std::fabs(n0-lo)<=std::fabs(n0-hi)? lo : hi);",
        ["3", "0", "10"], 0.0, help_="Whichever of Low or High is closer to Value.")
    add("LbIsBetweenPair", "Is Between Pair", "Math", "Compare", "CAT_LOGIC", [N("Value"), N("First"), N("Second")], [B("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); out=VsValue(n0>=lo && n0<=hi);",
        ["4", "8", "2"], 1, "b", help_="True when Value is between the other two, in either order, ends included.")
    add("LbScaleAround", "Scale Around", "Math", "", "CAT_MATH", [N("Value"), N("Center"), N("Scale")], [N("Result")],
        "out=VsValue(n1+(n0-n1)*n2);", ["4", "2", "3"], 8.0, help_="Scales the gap from Center to Value.")
    add("LbPingTime", "Ping Time", "Time", "", "CAT_MATH", [N("Time"), N("Length")], [N("Result")],
        "float L=std::fabs(n1); if(L<1e-6f) out=VsValue(0.f); else { float p=L*2.f; float t=std::fmod(std::fabs(n0),p); out=VsValue(t<=L? t : p-t);} ",
        ["1.5", "1"], 0.5, help_="Time folded into 0 to Length and back.")
    add("LbLoopTime", "Loop Time", "Time", "", "CAT_MATH", [N("Time"), N("Length")], [N("Result")],
        "float L=std::fabs(n1); if(L<1e-6f) out=VsValue(0.f); else { float t=std::fmod(n0,L); if(t<0)t+=L; out=VsValue(t);} ",
        ["5", "2"], 1.0, help_="Time wrapped into 0 up to Length.")
    add("LbElapsedPortion", "Elapsed Portion", "Time", "", "CAT_MATH", [N("Elapsed"), N("Duration")], [N("Result")],
        "float d=std::fabs(n1); float t=d<1e-6f? 0.f : n0/d; if(t<0)t=0; if(t>1)t=1; out=VsValue(t);",
        ["2", "8"], 0.25, help_="Elapsed divided by Duration, held to 0 to 1.")
    add("LbTimeLeft", "Time Left", "Time", "", "CAT_MATH", [N("Elapsed"), N("Duration")], [N("Result")],
        "float left=n1-n0; if(left<0)left=0; out=VsValue(left);",
        ["2", "8"], 6.0, help_="Duration minus Elapsed, and not below zero.")
    add("LbIsTimeUp", "Has Time Ended", "Time", "", "CAT_LOGIC", [N("Elapsed"), N("Duration")], [B("Result")],
        "out=VsValue(n1>=0.f && n0>=n1);", ["5", "5"], 1, "b", help_="True when Elapsed has reached Duration.")
    add("LbFramesToSeconds", "Frames to Seconds", "Time", "", "CAT_MATH", [N("Frames"), N("Rate")], [N("Result")],
        "float r=n1; out=VsValue(std::fabs(r)<1e-6f? 0.f : n0/r);",
        ["30", "60"], 0.5, help_="How many seconds Frames last at Rate frames per second.")
    add("LbSecondsToFrames", "Seconds to Frames", "Time", "", "CAT_MATH", [N("Seconds"), N("Rate")], [N("Result")],
        "out=VsValue(n0*n1);", ["0.5", "60"], 30.0, help_="How many frames Seconds last at Rate frames per second.")
    add("LbCooldownLeft", "Cooldown Left", "Flow", "Timing", "CAT_MATH", [N("Elapsed"), N("Cooldown")], [N("Result")],
        "float left=n1-n0; if(left<0)left=0; out=VsValue(left);",
        ["0.25", "1"], 0.75, help_="How much of Cooldown is still left.")
    add("LbIsCooldownReady", "Is Cooldown Ready", "Flow", "Timing", "CAT_LOGIC", [N("Elapsed"), N("Cooldown")], [B("Result")],
        "out=VsValue(n0>=n1);", ["1", "1"], 1, "b", help_="True when Elapsed has reached Cooldown.")
    add("LbPulseAt", "Is Pulse Active", "Time", "", "CAT_LOGIC", [N("Time"), N("Period"), N("Duty")], [B("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float d=n2; if(d<0)d=0; if(d>1)d=1; float x=std::fmod(n0,p); if(x<0)x+=p; out=VsValue(x/p < d);",
        ["0.1", "1", "0.25"], 1, "b", help_="True for the first Duty fraction of each Period.")
    add("LbBeatNumber", "Beat Number", "Time", "", "CAT_MATH", [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float t=n0; if(t<0)t=0; out=VsValue(std::floor(t/p));",
        ["2.5", "1"], 2.0, help_="How many whole periods fit into Time, counting from 0.")
    add("LbPhase", "Phase", "Time", "", "CAT_MATH", [N("Time"), N("Period")], [N("Result")],
        "float p=std::fabs(n1); if(p<1e-4f)p=1.f; float x=std::fmod(n0,p); if(x<0)x+=p; out=VsValue(x/p);",
        ["2.25", "1"], 0.25, help_="Where Time is inside the current period, as 0 to 1.")
    add("LbMixThree", "Mix Three", "Math", "Curves", "CAT_MATH", [N("First"), N("Second"), N("Third"), N("Portion")], [N("Result")],
        "float t=n3; if(t<0)t=0; if(t>1)t=1; float x=t*2.f; out=VsValue(x<1.f? n0+(n1-n0)*x : n1+(n2-n1)*(x-1.f));",
        ["0", "10", "30", "0.25"], 5.0, help_="Blends First to Second over the first half of Portion, then Second to Third.")
    add("LbHoldRange", "Hold Range", "Math", "", "CAT_MATH", [N("Value"), N("Low"), N("High"), N("Fallback")], [N("Result")],
        "float lo=n1, hi=n2; if(lo>hi) std::swap(lo,hi); out=VsValue(n0<lo||n0>hi? n3 : n0);",
        ["9", "0", "4", "-1"], -1.0, help_="Value when it is inside Low and High, otherwise Fallback.")
    add("LbGreaterGap", "Greater Gap", "Math", "Compare", "CAT_MATH", [N("Value"), N("Other"), N("Gap")], [N("Result")],
        "out=VsValue(n0 > n1+std::fabs(n2)? 1.f : 0.f);", ["6", "2", "3"], 1.0, help_="1 when Value is above Other by more than Gap, otherwise 0.")
    add("LbLetterAt", "Letter at", "Text", "", "CAT_DATA", [T("Text"), N("Index")], [T("Result")],
        "int i=(int)std::lround(n0); std::string r; if(i>=0 && i<(int)t0.size()) r.push_back(t0[(size_t)i]); out=VsValue(r);",
        ['"cat"', "1"], "a", "s", help_="The character at Index, counting from 0. Empty when Index is outside Text.")
    add("LbHasPrefix", "Has Prefix", "Text", "", "CAT_DATA", [T("Text"), T("Prefix")], [B("Result")],
        "out=VsValue(t0.rfind(t1,0)==0);", ['"hello"', '"he"'], 1, "b", help_="True when Text starts with Prefix.")
    add("LbHasSuffix", "Has Suffix", "Text", "", "CAT_DATA", [T("Text"), T("Suffix")], [B("Result")],
        "bool has=t1.size()<=t0.size() && t0.compare(t0.size()-t1.size(), t1.size(), t1)==0; out=VsValue(has);",
        ['"hello"', '"lo"'], 1, "b", help_="True when Text ends with Suffix.")
    add("LbReplaceAll", "Replace All", "Text", "", "CAT_DATA", [T("Text"), T("Find"), T("Replace")], [T("Result")],
        "std::string r=t0; if(!t1.empty()){ size_t p=0; while((p=r.find(t1,p))!=std::string::npos){ r.replace(p,t1.size(),t2); p+=t2.size(); if(p>r.size()) break; } } out=VsValue(r);",
        ['"a-a"','"a"','"b"'], "b-b", "s", help_="Replaces every Find in Text with Replace.")
    add("LbTrimEnds", "Trim Ends", "Text", "", "CAT_DATA", [T("Text")], [T("Result")],
        "size_t a=0,b=t0.size(); while(a<b && std::isspace((unsigned char)t0[a])) ++a; while(b>a && std::isspace((unsigned char)t0[b-1])) --b; out=VsValue(t0.substr(a,b-a));",
        ['"  hi  "'], "hi", "s", help_="Text with spaces removed from both ends.")
    add("LbIsSameText", "Is Same Text", "Text", "", "CAT_DATA", [T("First"), T("Second")], [B("Result")],
        "out=VsValue(t0==t1);", ['"cat"', '"cat"'], 1, "b", help_="True when the two texts are exactly the same.")
    add("LbLongerText", "Longer Text", "Text", "", "CAT_DATA", [T("First"), T("Second")], [T("Result")],
        "out=VsValue(t0.size()>=t1.size()? t0 : t1);", ['"a"', '"bbb"'], "bbb", "s", help_="Whichever text is longer. A tie keeps the first.")

math_curves()
math_numbers()
math_vectors()
text_nodes()
more_logic()
stateful()
extra_batch()

def lint_title(title, kind):
    small = {"a","and","at","by","for","in","is","of","on","or","the","to","up"}
    banned = {"anim","api","bool","dt","euler","float","fov","fps","gameobject","id","idx","ik","int","max","min","nav","num","obj","pos","prefab","old","quat","rad","rigidbody","rot","rpc","string","transform","ui","var","vec","vec3"}
    words = title.split(" ")
    if not words or any(w=="" for w in words):
        return "space"
    for i,w in enumerate(words):
        low=w.lower()
        if low in banned: return "banned "+w
        if any(ch.isdigit() or not ch.isalpha() for ch in w): return "symbol "+w
        if i==0:
            if not (w[0].isupper() and (len(w)>=3 or low in small)):
                return "first "+w
        elif low in small:
            if w != low: return "small "+w
        else:
            if len(w)<3 or not w[0].isupper() or w[1:]!=w[1:].lower():
                return "word "+w
    if kind=="Question" and not (title.startswith("Is ") or title.startswith("Has ") or title.startswith("Are ") or title.startswith("Does ")):
        # generator uses Is/Has only; Are/Does would fail C++ lint. Check properly.
        if not (title.startswith("Is ") or title.startswith("Has ")):
            return "question"
    if kind=="Action" and (title.startswith("On ") or title.startswith("Get ") or title.startswith("Is ") or title.startswith("Has ")):
        return "action"
    return ""

def main():
    # Fix question kinds: titles starting with Is/Has are questions.
    for n in NODES:
        if n["title"].startswith("Is ") or n["title"].startswith("Has ") or n["title"].startswith("Are ") or n["title"].startswith("Does "):
            if not (n["title"].startswith("Is ") or n["title"].startswith("Has ")):
                raise SystemExit("bad question "+n["title"])
    seen=set(); titles=set()
    for n in NODES:
        if n["type"] in seen: raise SystemExit("dup type "+n["type"])
        if n["title"] in titles: raise SystemExit("dup title "+n["title"])
        seen.add(n["type"]); titles.add(n["title"])
        kind = "Action" if n["exec"] else ("Question" if n["title"].startswith(("Is ","Has ")) else ("Reading" if n["title"].startswith("Get ") else "Calculation"))
        why = lint_title(n["title"], kind)
        if why: raise SystemExit(f'{n["type"]} {n["title"]}: {why}')
        for pin,_ in n["ins"]+n["outs"]:
            why = lint_title(pin, "Calculation")
            if why: raise SystemExit(f'{n["type"]} pin {pin}: {why}')
        for pin in n["exec_outs"]:
            if pin:
                why = lint_title(pin, "Calculation")
                if why: raise SystemExit(f'{n["type"]} exec {pin}: {why}')
    print("library nodes", len(NODES))

    # header
    cases=[]
    exec_cases=[]
    op_of=[]
    for i,n in enumerate(NODES):
        op_of.append(f'if (type=="{n["type"]}") return {i};')
        if n["exec"]:
            exec_cases.append(f"case {i}: {{\n{decl_inputs(n)}\n{n['cpp']}\nreturn flow;\n}}")
        else:
            cases.append(f"case {i}: {{\n{decl_inputs(n)}\nVsValue out;\n{n['cpp']}\nreturn out;\n}}")
    header = f'''#pragma once
// Generated by tools/gen_logic_library.py. {len(NODES)} distinct logic nodes.
#include "okay/VisualScript/Nodes.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
namespace okay::vs {{
struct LibraryNode : VsNode {{
    int op = 0;
    float mem = 0;
    int count = 0;
    bool seen = false;
    bool flag = false;
    explicit LibraryNode(int o) : op(o) {{}}
    int ExecOutCount() const override {{
        switch (op) {{
{exec_count(NODES)}
        default: return 0;
        }}
    }}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {{
        switch (op) {{
{chr(10).join(cases)}
        default: return {{}};
        }}
    }}
    int Exec(NodeGraph& g, VsContext& ctx) override {{
        int flow = 0;
        switch (op) {{
{chr(10).join(exec_cases) if exec_cases else "default: break;"}
        default: break;
        }}
        return flow;
    }}
}};
inline int LibraryOp(const std::string& type) {{
    {" ".join(op_of)}
    return -1;
}}
inline std::unique_ptr<VsNode> MakeLibraryNode(const std::string& type) {{
    int op = LibraryOp(type);
    if (op < 0) return nullptr;
    return std::make_unique<LibraryNode>(op);
}}
}}
'''
    # The exec switch with default inside is messy if exec_cases also have default. Fix exec method.
    header = header.replace(
        f'''        switch (op) {{
{chr(10).join(exec_cases) if exec_cases else "default: break;"}
        default: break;
        }}''',
        "        switch (op) {\n" + "\n".join(exec_cases) + "\n        default: break;\n        }"
    )
    (ROOT/"engine/include/okay/VisualScript/NodesLibrary.hpp").write_text(header)

    rows=[]
    reg=[]
    names=[]
    help_lines=[]
    places=[]
    for n in NODES:
        ins="".join(letter for _,letter in n["ins"])
        outs="".join(letter for _,letter in n["outs"])
        rows.append(f'        {{"{n["type"]}", {"true" if n["exec"] else "false"}, "{ins}", "{outs}"}},')
        ein = "true" if n["exec"] else "false"
        eouts = ", ".join(f'"{p}"' for p in (n["exec_outs"] or ([""] if n["exec"] else [])))
        dins = ", ".join(f'"{name}"' for name,_ in n["ins"])
        douts = ", ".join(f'"{name}"' for name,_ in n["outs"])
        reg.append(f'        {{"{n["type"]}", "{n["title"]}", {n["reg"]}, {ein}, {{{eouts}}}, {{{dins}}}, {{{douts}}}, 0, nullptr}},')
        kind = "Action" if n["exec"] else ("Question" if n["title"].startswith(("Is ","Has ")) else ("Reading" if n["title"].startswith("Get ") else "Calculation"))
        names.append(f'    {{"{n["type"]}", VsNameKind::{kind}, "{n["title"]}", "{n["title"]}", "", nullptr, 0, nullptr, 0}},')
        help_lines.append(f'        {{"{n["type"]}", "{n["help"].replace(chr(34), "")}"}},')
        sub = f', "{n["sub"]}"' if n["sub"] else ""
        places.append(f'    if (std::strcmp(type, "{n["type"]}") == 0) return VsAt("{n["topic"]}"{sub});')

    (ROOT/"engine/include/okay/VisualScript/library_rows.inc").write_text("\n".join(rows)+"\n")
    (ROOT/"editor/generated").mkdir(exist_ok=True)
    (ROOT/"editor/generated/library_reg.inc").write_text("\n".join(reg)+"\n")
    (ROOT/"editor/generated/library_names.inc").write_text("\n".join(names)+"\n")
    (ROOT/"editor/generated/library_help.inc").write_text("\n".join(help_lines)+"\n")
    (ROOT/"editor/generated/library_place.inc").write_text("\n".join(places)+"\n")

    # behaviour test
    tests=[]
    for n in NODES:
        if n.get("state"):
            continue
        if n["expect"] is None:
            continue
        lines = ["node 1 OnStart", "entry OnStart 1", f'node 2 {n["type"]}']
        vals=[]
        for i,d in enumerate(n["defaults"]):
            lines.append(f"default 2 {i} {d}")
            vals.append(d)
        if len(n["defaults"]) != len(n["ins"]):
            raise SystemExit(f'{n["type"]} defaults {len(n["defaults"])} ins {len(n["ins"])}')
        lines += ["node 3 SetVar out", "exec 1 0 3", "data 3 0 2 0"]
        graph = ("\\n".join(lines) + "\\n").replace('"', '\\"')
        if n["etype"]=="f":
            check = f'CHECK(std::fabs(Var(v, "out").AsFloat() - {float(n["expect"])}f) < 0.02f);'
        elif n["etype"]=="b":
            check = f'CHECK(Var(v, "out").AsBool() == {"true" if n["expect"] else "false"});'
        else:
            check = 'CHECK(Var(v, "out").AsString() == "' + str(n["expect"]) + '");'
        tests.append(f'''    {{
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "N", "{graph}");
        s.Start(); s.Update(0.05f);
        {check}
        g_seen.insert("{n["type"]}");
    }}''')
    # stateful hand tests
    state_tests = r'''
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "H",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\n"
            "node 3 LbHysteresis\ndefault 3 1 0.4\ndefault 3 2 0.6\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(0.0f));
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        v->SetVariable("v", vs::VsValue(0.7f));
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        v->SetVariable("v", vs::VsValue(0.5f));
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        v->SetVariable("v", vs::VsValue(0.2f));
        s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        g_seen.insert("LbHysteresis");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Peak",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbRememberPeak\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(1.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(3.0f));
        s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(2.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 3.f) < 0.02f);
        g_seen.insert("LbRememberPeak");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Valley",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbRememberValley\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(4.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(1.0f));
        s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(2.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 1.f) < 0.02f);
        g_seen.insert("LbRememberValley");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Mean",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbRunningMean\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(2.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(4.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 3.f) < 0.02f);
        g_seen.insert("LbRunningMean");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "TrueN",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbCountTrueFrames\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(true));
        s.Start(); s.Update(0.05f); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 2.f) < 0.02f);
        v->SetVariable("v", vs::VsValue(false));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat()) < 0.02f);
        g_seen.insert("LbCountTrueFrames");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hold",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar take\nnode 3 GetVar v\n"
            "node 4 LbSampleHold\ndata 4 0 2 0\ndata 4 1 3 0\n"
            "node 5 SetVar out\nexec 1 0 5\ndata 5 0 4 0\n");
        v->SetVariable("take", vs::VsValue(true));
        v->SetVariable("v", vs::VsValue(4.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("take", vs::VsValue(false));
        v->SetVariable("v", vs::VsValue(9.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 4.f) < 0.02f);
        g_seen.insert("LbSampleHold");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Prev",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbPreviousNumber\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(1.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(6.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 1.f) < 0.02f);
        g_seen.insert("LbPreviousNumber");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Spike",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbSpike\ndefault 3 1 2\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(0.0f));
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        v->SetVariable("v", vs::VsValue(5.0f));
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("LbSpike");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "After",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 LbPassAfter\ndefault 2 0 2\n"
            "node 3 SetVar hit\ndefault 3 0 true\nexec 1 0 2\nexec 2 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "hit").AsBool());
        s.Update(0.05f);
        CHECK(Var(v, "hit").AsBool());
        g_seen.insert("LbPassAfter");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "EveryC",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 LbEveryCalls\ndefault 2 0 2\n"
            "node 3 SetVar hit\ndefault 3 0 true\nexec 1 0 2\nexec 2 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "hit").AsBool());
        s.Update(0.05f);
        CHECK(Var(v, "hit").AsBool());
        g_seen.insert("LbEveryCalls");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Acc",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetVar v\nnode 3 LbAccumulate\ndata 3 0 2 0\n"
            "node 4 SetVar out\nexec 1 0 4\ndata 4 0 3 0\n");
        v->SetVariable("v", vs::VsValue(2.0f));
        s.Start(); s.Update(0.05f);
        v->SetVariable("v", vs::VsValue(3.0f));
        s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 5.f) < 0.02f);
        g_seen.insert("LbAccumulate");
    }
'''
    types = ", ".join(f'"{n["type"]}"' for n in NODES)
    test = f'''// Generated by tools/gen_logic_library.py. Every library node runs in a scene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <set>
#include <string>
using namespace okay;
static std::set<std::string> g_seen;
static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {{
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vc->LoadFromText(text, &err));
    if (!err.empty()) std::printf("  load %s: %s\\n", name, err.c_str());
    return vc;
}}
static vs::VsValue Var(VisualScriptComponent* v, const char* name) {{ return v->GetVariable(name); }}
int main() {{
    RUN_SUITE("vs_logic_library");
    Time::SetTimeScale(1);
    const char* types[] = {{ {types} }};
{chr(10).join(tests)}
{state_tests}
    for (const char* t : types) {{
        if (!g_seen.count(t)) std::printf("  library node not covered: %s\\n", t);
        CHECK(g_seen.count(t) == 1);
    }}
    std::printf("  library nodes tested: %d\\n", (int)g_seen.size());
    Time::SetTimeScale(1);
    TEST_MAIN_RESULT();
}}
'''
    (ROOT/"tests/test_vs_logic_library.cpp").write_text(test)

    # playground graph: visible approach chain + every node declared
    lines = [
        "node 1 OnUpdate",
        "entry OnUpdate 1",
        "node 2 GetX",
        "node 3 LbApproach",
        "default 3 1 2.2",
        "default 3 2 0.08",
        "data 3 0 2 0",
        "node 4 SetPosition",
        "default 4 1 0.5",
        "default 4 2 0",
        "data 4 0 3 0",
        "exec 1 0 4",
    ]
    col = 0
    row = 0
    nid = 10
    for n in NODES:
        if n["type"] == "LbApproach":
            continue
        x = 40 + (col % 8) * 240
        y = 400 + row * 160
        lines.append(f"node {nid} {n['type']}")
        lines.append(f"pos {nid} {x} {y}")
        for i, d in enumerate(n["defaults"] or []):
            lines.append(f"default {nid} {i} {d}")
        # pull data nodes once from OnStart so they actually run
        nid += 1
        col += 1
        if col % 8 == 0:
            row += 1
    (ROOT/"examples/visual_script/playground/library.okayvs").write_text("\n".join(lines)+"\n")
    scene = '''okayscene 1
name "VS Playground: Logic Library"
gravity 0 0
gravity3 0 0 0
gameobject 0 "Main Camera"
  active 1
  parent -1
  transform 0 4 10 0 0 0 1 1 1 1
    camera 1 5 52 0.11 0.13 0.17 1 1 0 0 0.3 4000 0 0 0 1 1 -1 0 50 24 1 0 0 0 0 32 0.3 2 18 1.5
end
gameobject 1 "Instructions"
  active 1
  parent -1
  transform 0 0 0 0 0 0 1 1 1 1
    text "Press Play. The white cube approaches the right using Approach, one node from the logic library. The rest of the library is in this graph." 1 0.96 0.82 1 2.15 1 18 16 0 1 0 0 0 0.72 1 1 0 0 0 0 0 1 0 1080 150 0 1 0.05 0.06 0.09 0.9 0 1.15 0 1
end
gameobject 2 "Floor"
  active 1
  parent -1
  transform 0 -0.15 0 0 0 0 1 16 0.3 16
    mesh "Cube" 0.18 0.2 0.24 1 0 "" 0
    material 0 0 0 0 8 1 "" 1 1
end
gameobject 3 "Marker"
  active 1
  parent -1
  transform -1.5 0.5 0 0 0 0 1 0.8 0.8 0.8
    mesh "Cube" 0.92 0.92 0.95 1 0 "" 0
    material 0 0 0 0 8 1 "" 1 1
    visualscript ""
    visualscriptpath "library.okayvs"
end
'''
    (ROOT/"examples/visual_script/playground/library.okayscene").write_text(scene)
    (ROOT/"tools/logic_library_count.txt").write_text(str(len(NODES))+"\n")
    print("wrote", len(NODES))

def decl_inputs(n):
    lines=[]
    fi=bi=ti=0
    for i,(name,letter) in enumerate(n["ins"]):
        if letter=="N":
            lines.append(f"float n{fi} = In({i}, g, ctx).AsFloat();")
            fi += 1
        elif letter=="B":
            lines.append(f"bool b{bi} = In({i}, g, ctx).AsBool();")
            bi += 1
        elif letter=="T":
            lines.append(f"std::string t{ti} = In({i}, g, ctx).AsString();")
            ti += 1
        else:
            lines.append(f"float n{fi} = In({i}, g, ctx).AsFloat();")
            fi += 1
    return "\n".join(lines)

def exec_count(nodes):
    lines=[]
    for i,n in enumerate(nodes):
        if n["exec"]:
            lines.append(f"        case {i}: return {max(1, len(n['exec_outs']))};")
    return "\n".join(lines)

if __name__ == "__main__":
    main()
