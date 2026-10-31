/*
    Game state uniforms shared by the compute shaders (see bismuth_shared.sh for the layout).
*/
uniform vec4 u_gameState[GAME_STATE_VEC4S];

#define BK_GROUP_STATE_OFFSET  uint(u_gameState[5].x)
#define BK_PULSE_OFFSET        uint(u_gameState[5].y)
#define BK_INPUT_SPRITE_OFFSET uint(u_gameState[5].z)
#define BK_WORK_GROUP_OFFSET   uint(u_gameState[5].w)
#define BK_LAYER_OFFSET        uint(u_gameState[6].x)
#define BK_WORK_GROUP_COUNT    uint(u_gameState[6].y)
#define BK_LAYER_COUNT         uint(u_gameState[6].z)
