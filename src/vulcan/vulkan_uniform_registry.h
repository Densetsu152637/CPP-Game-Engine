//
// Compatibility aliases for the backend-neutral rendering uniform registry.
//

#pragma once

#include "../rendering/uniform_registry.h"

namespace vulkan
{
    using UniformKind = rendering::UniformKind;
    using UniformBinding = rendering::UniformBinding;
    using UniformValue = rendering::UniformValue;
    using UniformUpload = rendering::UniformUpload;
    using UniformRegistry = rendering::UniformRegistry;
}
