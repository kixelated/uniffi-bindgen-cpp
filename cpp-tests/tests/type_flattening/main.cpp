#include "test_common.hpp"

#include <type_flattening.hpp>

int main() {
    int value = 42;

    auto structure = type_flattening::get_struct(value);
    ASSERT_NE(structure.optional_arc, nullptr);
    ASSERT_EQ(structure.optional_arc->get_value(), value);
    ASSERT_TRUE(structure.has_object());

    auto roundtrip = type_flattening::struct_roundtrip(structure);
    ASSERT_NE(roundtrip.optional_arc, nullptr);
    ASSERT_EQ(roundtrip.optional_arc->get_value(), value);

    ASSERT_EQ(structure.adjusted_value(8), 50);
    try {
        structure.adjusted_value(-1);
        ASSERT_TRUE(false);
    } catch (const type_flattening::adjustment_error::NegativeAdjustment& error) {
        ASSERT_TRUE(error.is_negative());
        ASSERT_EQ(error.adjustment, -1);
    }

    auto next = type_flattening::next(type_flattening::FlatValue::kFirst);
    ASSERT_EQ(next, type_flattening::FlatValue::kSecond);

    ASSERT_TRUE(type_flattening::eq(type_flattening::FlatValue::kFirst,
                                    type_flattening::FlatValue::kFirst));
    ASSERT_TRUE(type_flattening::ne(type_flattening::FlatValue::kFirst,
                                    type_flattening::FlatValue::kSecond));
    ASSERT_EQ(type_flattening::hash(type_flattening::FlatValue::kFirst),
              type_flattening::hash(type_flattening::FlatValue::kFirst));
    ASSERT_TRUE(type_flattening::cmp(type_flattening::FlatValue::kFirst,
                                     type_flattening::FlatValue::kSecond) < 0);

    type_flattening::RichValue rich = type_flattening::RichValue::kText { "hello" };
    ASSERT_EQ(rich.describe(), "text:hello");

    return 0;
}
