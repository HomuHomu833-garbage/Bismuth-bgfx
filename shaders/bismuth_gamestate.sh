/*
    Unpacks u_gameState (see bismuth_uniforms.sh) into the kernel's BkGameState.
    Include after bismuth_kernel.sh.
*/
BkGameState bkLoadGameState()
{
    BkGameState gs;
    gs.timer               = u_gameState[0].x;
    gs.audioScale          = u_gameState[0].y;
    gs.cameraViewSize      = vec2(u_gameState[0].z, u_gameState[0].w);
    gs.cameraPosition      = vec2(u_gameState[1].x, u_gameState[1].y);
    gs.cameraRight         = vec2(u_gameState[1].z, u_gameState[1].w);
    gs.cameraUp            = vec2(u_gameState[2].x, u_gameState[2].y);
    gs.cameraSize          = vec2(u_gameState[2].z, u_gameState[2].w);
    gs.winSize             = vec2(u_gameState[3].x, u_gameState[3].y);
    gs.screenRight         = u_gameState[3].z;
    gs.cameraUnzoomedX     = u_gameState[3].w;
    gs.specialLightBGColor = vec3(u_gameState[4].x, u_gameState[4].y, u_gameState[4].z);
    gs.flags               = uint(u_gameState[4].w);
    return gs;
}

