#include "test/ecs/ecs_test_fixtures.h"

using namespace ecs_test;

void test_sparse_bit_field_tracks_sparse_pages()
{
    SparseBitField bits;
    const size_t secondPageBit = SparseBitField::BITS_PER_PAGE + 1;

    require(bits.empty(), "new sparse bit field should be empty");
    require(!bits.at(secondPageBit), "missing sparse bit page should read false");

    bits.set(5, true);
    bits.set(secondPageBit, true);

    require(bits.size() == 2, "sparse bit field did not count set bits");
    require(bits.at(5), "sparse bit field missed bit on first page");
    require(bits.at(secondPageBit), "sparse bit field missed bit on later page");
    require(!bits.at(secondPageBit + 1), "sparse bit field reported unset bit as true");

    bits.set(5, false);
    require(bits.size() == 1, "sparse bit field did not clear first-page bit");

    bits.set(secondPageBit, false);
    require(bits.empty(), "sparse bit field did not release final set bit");
    require(!bits.at(secondPageBit), "cleared sparse bit should read false");
    require_throws(
        [&]()
        {
            (void)bits.at_packed(0);
        },
        "sparse bit field kept an empty page initialised"
    );
}

void test_sparse_bit_field_packed_and_bitwise_operations()
{
    SparseBitField left;
    SparseBitField right;
    const size_t distantBit = SparseBitField::BITS_PER_PAGE * 3 + 2;

    left.set(1, true);
    left.set(distantBit, true);
    right.set(7, true);
    right.set(distantBit, true);

    SparseBitField both = left & right;
    require(both.size() == 1, "sparse bit field intersection had wrong size");
    require(both.at(distantBit), "sparse bit field intersection missed shared bit");
    require(!both.at(1), "sparse bit field intersection kept left-only bit");
    require(!both.at(7), "sparse bit field intersection kept right-only bit");

    SparseBitField either = left | right;
    require(either.size() == 3, "sparse bit field union had wrong size");

    ArrayList<size_t> indexes = either.trueIndexes();
    require(indexes.length() == 3, "sparse bit field true index enumeration had wrong size");
    require(indexes[0] == 1, "sparse bit field true index enumeration missed first bit");
    require(indexes[1] == 7, "sparse bit field true index enumeration missed second bit");
    require(indexes[2] == distantBit, "sparse bit field true index enumeration missed distant bit");

    SparseBitField packed;
    packed.setPacked(2, SparseBitField::PACKED_TYPE {0b101});
    require(packed.at(SparseBitField::PACKED_SIZE * 2), "packed sparse bit write missed low bit");
    require(packed.at(SparseBitField::PACKED_SIZE * 2 + 2), "packed sparse bit write missed high bit");
    require(packed.size() == 2, "packed sparse bit write had wrong size");

    packed.setPacked(2, 0);
    require(packed.empty(), "zero packed sparse bit write did not clear bits");

    packed.at_packed<true>(4) = SparseBitField::PACKED_TYPE {1} << 3;
    require(packed.at(SparseBitField::PACKED_SIZE * 4 + 3), "guaranteed packed access did not create page");
    packed.at_packed<true>(4) = 0;
    require(packed.empty(), "sparse bit field size did not reflect direct packed clear");
    packed.release_empty_pages();
    require_throws(
        [&]()
        {
            (void)packed.at_packed(4);
        },
        "sparse bit field did not release direct-zeroed page"
    );
}

void test_component_type_ids_are_stable_and_distinct()
{
    const ecs::ComponentTypeId firstPositionId = ecs::component_type_id<Position>();
    const ecs::ComponentTypeId secondPositionId = ecs::component_type_id<Position>();
    const ecs::ComponentTypeId velocityId = ecs::component_type_id<Velocity>();

    require(firstPositionId == secondPositionId, "component type ID was not stable for the same type");
    require(firstPositionId != velocityId, "component type IDs were not distinct across component types");
}
