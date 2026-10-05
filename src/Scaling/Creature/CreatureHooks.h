#ifndef CREATUREHOOKS_H
#define CREATUREHOOKS_H

#include "Creature.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

// Type alias for variadic event hooks
template<typename... Args>
using CreatureHook = std::function<void(Args...)>;

// Type alias for a list of hooks that take varying arguments
template<typename... Args>
using HandlersList = std::vector<CreatureHook<Args...>>;

// HandlerMap using variadic templates
template<typename... Args>
using HandlerMap = std::unordered_map<uint32, HandlersList<Args...>>;

class CreatureHooks
{
private:

    CreatureHooks():
        _OnSpawnHandlers(std::make_unique<HandlerMap<Creature*>>()),
        _JustDiedHandlers(std::make_unique<HandlerMap<Creature*, Unit*>>()),
        _OnAddToInstanceHandlers(std::make_unique<HandlerMap<Creature*>>())
    {
        _OnSpawnHandlers->reserve(128);
        _JustDiedHandlers->reserve(128);
        _OnAddToInstanceHandlers->reserve(100);
    }

    ~CreatureHooks() = default;

    // ensure we only ever have one instance of this class
    CreatureHooks(CreatureHooks const&) = delete;
    CreatureHooks& operator=(CreatureHooks const&) = delete;

    // Data members for storing event handlers
    std::unique_ptr<HandlerMap<Creature*>> _OnSpawnHandlers;
    std::unique_ptr<HandlerMap<Creature*, Unit*>> _JustDiedHandlers;
    std::unique_ptr<HandlerMap<Creature*>> _OnAddToInstanceHandlers;

public:
    static CreatureHooks* instance()
    {
        static CreatureHooks instance;

        return &instance;
    }

    // Register events for specific actions
    void RegisterJustDied(uint32 entry, CreatureHook<Creature*, Unit*> callback);
    void RegisterOnSpawn(uint32 entry, CreatureHook<Creature*> callback);
    void RegisterOnAddToInstance(uint32 entry, CreatureHook<Creature*> callback);

    // Event triggers
    void JustDied(Creature* creature, Unit* killer);
    void JustSpawned(Creature* creature);
    void AddToInstance(Creature* creature);
};

#define sCreatureHooks CreatureHooks::instance()

#endif // CREATUREHOOKS_H
