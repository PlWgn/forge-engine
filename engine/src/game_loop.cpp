// Game-frame orchestration extracted from runtime.cpp; licensed core origin.
#include <forge/engine.hpp>
#include <forge/animation.hpp>
#include <forge/physics.hpp>
#include <forge/particles.hpp>
#include <chrono>
namespace forge {
void Runtime::tick(float& physicsAccumulator){
    auto frameStart=std::chrono::steady_clock::now();
    auto elapsed=[](auto before){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();};
    if(!editing || !gamePaused)dispatchFrame();
    profile["callbacks_ms"]=elapsed(frameStart);
    auto audioStart=std::chrono::steady_clock::now();
    audio.update(dt);
    profile["audio_ms"]=elapsed(audioStart);
    auto scriptsStart=std::chrono::steady_clock::now();
    destroyDead();attachPending();destroyDead();
    if(!gamePaused){
        // Own callback snapshots: lifecycle may add/remove entities and scripts.
        auto current=scripts;
        for(auto& module:startup)
            if(py::hasattr(module,"on_update"))module.attr("on_update")(dt);
        for(auto& script:current)
            if((!script.entity || script.entity->alive) && py::hasattr(script.instance,"on_update"))
                script.instance.attr("on_update")(dt);
        destroyDead();attachPending();destroyDead();
        current=scripts;
        profile["scripts_ms"]=elapsed(scriptsStart);
        auto physicsStart=std::chrono::steady_clock::now();
        physicsAccumulator+=dt;
        if(!world.physicsEnabled)physicsAccumulator=0;
        std::unique_ptr<PhysicsForces> forces;
        if(world.physicsEnabled && physicsAccumulator>=1.0f/120)
            forces=std::make_unique<PhysicsForces>(world);
        while(world.physicsEnabled && physicsAccumulator>=1.0f/120){
            auto previous=world.contacts;
            forces->step(1.0f/120);
            physicsAccumulator-=1.0f/120;
            dispatchContacts(current,previous);
        }
        profile["physics_ms"]=elapsed(physicsStart);
    }
    auto particleStart=std::chrono::steady_clock::now();
    if(!gamePaused && world.particles)world.particles->update(world,dt);
    profile["particles_ms"]=elapsed(particleStart);
    destroyDead();attachPending();destroyDead();
    if(!gamePaused)updateAnimations(*this,dt);
    refreshLocalizedEntities();localization.flush();
    if(renderer)renderer->render(world);
    profile["frame_ms"]=elapsed(frameStart);
    profile["entities"]=world.entities.size();profile["assets"]=assets.stats();
    if(world.particles)profile["particles"]=world.particles->stats();
    if(world.physics3d)profile["physics"]=world.physics3d->stats();
    if(renderer)profile["renderer"]=renderer->diagnostics();
}
}
