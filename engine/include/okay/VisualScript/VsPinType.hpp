#pragma once
// What a visual-script data pin carries.
namespace okay::vs {

/// What a data pin carries. Stored as one letter per pin in the node table.
enum class PinType : char {
    Any = 'A',     ///< anything (variables, events, Select, Equals...)
    Number = 'N',
    Bool = 'B',
    Text = 'T',
    Vec3 = 'V',
    Object = 'O',  ///< an object reference, or a name typed into the pin
    List = 'L',
    Map = 'M',     ///< a string-keyed map of values (a struct is a map with declared fields)
    Color = 'C',   ///< an RGBA colour
};

} // namespace okay::vs
