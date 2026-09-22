#include "test_common.hpp"

#include <trait_methods.hpp>

void test_trait_methods() {
    auto trait = trait_methods::TraitMethods::init("trait1");
    ASSERT_EQ(trait->to_string(), "TraitMethods(trait1)");
    ASSERT_EQ(trait->to_debug_string(), "TraitMethods { val: \"trait1\" }");
    ASSERT_EQ(trait->hash(), 8148112548604738188);

    auto ptr_copy = trait;
    auto trait_copy = trait_methods::TraitMethods::init("trait1");
    ASSERT_EQ(trait->to_string(), trait_copy->to_string());
    ASSERT_EQ(trait->to_debug_string(), trait_copy->to_debug_string());
    ASSERT_EQ(trait->hash(), trait_copy->hash());

    // Two different shared ptr's should differ
    ASSERT_NE(trait, trait_copy);

    // Two shared ptr's pointing to the same object should be equal
    ASSERT_EQ(trait, ptr_copy);

    // Internal equality check should work
    ASSERT_TRUE(trait->eq(trait_copy));
    ASSERT_FALSE(trait->ne(trait_copy));

    auto trait2 = trait_methods::TraitMethods::init("trait2");
    ASSERT_NE(trait, trait2);
    ASSERT_FALSE(trait->eq(trait2));
    ASSERT_TRUE(trait->ne(trait2));

    ASSERT_NE(trait->hash(), trait2->hash());
    ASSERT_NE(trait->to_string(), trait2->to_string());
    ASSERT_NE(trait->to_debug_string(), trait2->to_debug_string());
}

void test_proc_methods() {
    auto trait = trait_methods::ProcTraitMethods::init("trait1");
    ASSERT_EQ(trait->to_string(), "ProcTraitMethods(trait1)");
    ASSERT_EQ(trait->to_debug_string(), "ProcTraitMethods { val: \"trait1\" }");
    ASSERT_EQ(trait->hash(), 8148112548604738188);

    auto ptr_copy = trait;
    auto trait_copy = trait_methods::ProcTraitMethods::init("trait1");
    ASSERT_EQ(trait->to_string(), trait_copy->to_string());
    ASSERT_EQ(trait->to_debug_string(), trait_copy->to_debug_string());
    ASSERT_EQ(trait->hash(), trait_copy->hash());

    // Two different shared ptr's should differ
    ASSERT_NE(trait, trait_copy);

    // Two shared ptr's pointing to the same object should be equal
    ASSERT_EQ(trait, ptr_copy);

    // Internal equality check should work
    ASSERT_TRUE(trait->eq(trait_copy));
    ASSERT_FALSE(trait->ne(trait_copy));

    auto trait2 = trait_methods::ProcTraitMethods::init("trait2");
    ASSERT_NE(trait, trait2);
    ASSERT_FALSE(trait->eq(trait2));
    ASSERT_TRUE(trait->ne(trait2));

    ASSERT_NE(trait->hash(), trait2->hash());
    ASSERT_NE(trait->to_string(), trait2->to_string());
    ASSERT_NE(trait->to_debug_string(), trait2->to_debug_string());
}

void test_record_trait_methods() {
    trait_methods::TraitRecord first { "same", 1 };
    trait_methods::TraitRecord second { "same", 2 };
    trait_methods::TraitRecord different { "different", 1 };

    ASSERT_EQ(first.to_debug_string(), "TraitRecord { s: \"same\", i: 1 }");
    ASSERT_TRUE(first.eq(second));
    ASSERT_FALSE(first.ne(second));
    ASSERT_FALSE(first.eq(different));
    ASSERT_EQ(first.hash(), second.hash());
    ASSERT_EQ(first.cmp(second), 0);
}

void test_enum_trait_methods() {
    trait_methods::TraitEnum first = trait_methods::TraitEnum::kS { "one" };
    trait_methods::TraitEnum second = trait_methods::TraitEnum::kS { "two" };
    trait_methods::TraitEnum integer = trait_methods::TraitEnum::kI { 1 };

    ASSERT_EQ(first.to_string(), "TraitEnum::S(\"one\")");
    ASSERT_EQ(first.to_debug_string(), "S(\"one\")");
    ASSERT_TRUE(first.eq(second));
    ASSERT_EQ(first.hash(), second.hash());
    ASSERT_TRUE(first.cmp(integer) < 0);

    auto flat = trait_methods::get_flat_trait_enum(0);
    ASSERT_EQ(trait_methods::to_string(flat), "FlatTraitEnum::flat-alpha");
    ASSERT_EQ(trait_methods::to_debug_string(flat), "Alpha");
}

void test_error_trait_methods() {
    try {
        trait_methods::throw_api_failure(0);
    } catch (const trait_methods::ApiFailure &error) {
        ASSERT_EQ(error.to_string(), "api network issue");
        ASSERT_EQ(error.to_debug_string(), "NetworkIssue");
        static_cast<void>(error.hash());

        try {
            trait_methods::throw_api_failure(1);
        } catch (const trait_methods::ApiFailure &other) {
            ASSERT_FALSE(error.eq(other));
            ASSERT_TRUE(error.ne(other));
            ASSERT_TRUE(error.cmp(other) < 0);
            return;
        }
    }
    throw std::runtime_error("Expected ApiFailure");
}

int main() {
    test_trait_methods();
    test_proc_methods();
    test_record_trait_methods();
    test_enum_trait_methods();
    test_error_trait_methods();

    return 0;
}
