#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IdentityGen.hpp"
#include "../ContributedDescriptor.hpp"
#include "../DescriptorId.hpp"
#include "../Discovery.hpp"
#include "../NameId.hpp"
#include "../Origin.hpp"
#include "../TypeId.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanMetadata.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/Markers.hpp"
#include "ResolutionContext.hpp"

#include "DescriptorThunkGen.hpp"
#include "ReflectiveDataGen.hpp"
#include "DescriptorBuilderGen.hpp"
