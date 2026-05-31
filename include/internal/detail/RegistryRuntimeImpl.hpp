#pragma once

#include <algorithm>
#include <cassert>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "Registry.hpp"
#include "ResolutionContext.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/AnyBean.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/BeanMetadata.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

#include "RegistryRuntimeCoreImpl.hpp"
#include "RegistryResolutionImpl.hpp"
#include "RegistryScopedRuntimeImpl.hpp"
#include "RegistryThreadLocalImpl.hpp"
#include "RegistryStartupImpl.hpp"
