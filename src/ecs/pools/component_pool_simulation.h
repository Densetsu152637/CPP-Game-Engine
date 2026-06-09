//
// Simulation component-pool specializations.
//

#pragma once

#include "component_pool_base.h"

template <typename T>
class ComponentPool final : public SimulationComponentPoolBase<T, DirectComponentStorage<T>>
{};

template <typename T>
class BufferedComponentPool final : public SimulationComponentPoolBase<T, BufferedComponentStorage<T>>
{
    using base_type = SimulationComponentPoolBase<T, BufferedComponentStorage<T>>;

    bool m_swapPending = false;

public:
    void markDirty() override
    {
        base_type::markDirty();
        m_swapPending = true;
    }

    void markEntityDirty(const size_t entityIndex) override
    {
        base_type::markEntityDirty(entityIndex);
        m_swapPending = true;
    }

    void swapBuffers() override
    {
        if (!m_swapPending)
            return;

        base_type::swapBuffers();
        m_swapPending = false;
    }
};
