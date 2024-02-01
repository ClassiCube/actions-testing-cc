#ifndef CC_MAP_GEN_H
#define CC_MAP_GEN_H
#include "ExtMath.h"
#include "Vectors.h"
/* Implements flatgrass map generator, and original classic vanilla map generation (with perlin noise)
   Based on: https://github.com/UnknownShadow200/ClassiCube/wiki/Minecraft-Classic-map-generation-algorithm
   Thanks to Jerralish for originally reverse engineering classic's algorithm, then preparing a high level overview of the algorithm.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

/* Progress between 0 and 1 for the current step */
extern volatile float Gen_CurrentProgress;
/* Name of the current step being performed */
extern volatile const char* Gen_CurrentState;
extern int Gen_Seed;
extern BlockRaw* Gen_Blocks;

/* Starts generating a map using the Gen_Active generator */
void Gen_Start(void);
/* Checks whether the map generator has completed yet */
cc_bool Gen_IsDone(void);


struct MapGenerator {
	cc_bool (*Prepare)(void);
	void   (*Generate)(void);
};

extern const struct MapGenerator* Gen_Active;
extern const struct MapGenerator FlatgrassGen;
extern const struct MapGenerator NotchyGen;


extern BlockRaw* Tree_Blocks;
extern RNGState* Tree_Rnd;
/* Appropriate buffer size to hold positions and blocks generated by the tree generator. */
#define TREE_MAX_COUNT 96

/* Whether a tree can actually be generated at the given coordinates. */
cc_bool TreeGen_CanGrow(int treeX, int treeY, int treeZ, int treeHeight);
/* Generates the blocks (and their positions in the world) that actually make up a tree. */
/* Returns the number of blocks generated, which will be <= TREE_MAX_COUNT */
int  TreeGen_Grow(int treeX, int treeY, int treeZ, int height, IVec3* coords, BlockRaw* blocks);
#endif
