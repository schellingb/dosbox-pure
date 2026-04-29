#include "include/config.h"

struct MAPBucket
{
	const Bit8u* idents_compressed;
	Bit32u idents_size_compressed;
	Bit32u idents_size_uncompressed;
	const Bit8u* mappings_compressed;
	Bit32u mappings_size_compressed;
	Bit32u mappings_size_uncompressed;
	Bit32u mappings_action_offset;
};

enum { MAP_TABLE_SIZE = 4240, MAP_BUCKETS = 4 };
extern const Bit32u map_keys[MAP_TABLE_SIZE];
extern const MAPBucket map_buckets[MAP_BUCKETS];
