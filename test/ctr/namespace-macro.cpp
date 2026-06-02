#define CTORIUM_NAMESPACE ctrtest

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace namespace_macro_fixture {

struct [[=ctrtest::singleton{}]] Object {};

} // namespace namespace_macro_fixture

TEST(NamespaceMacro, CustomNamespaceResolvesSingleton) {
    auto& context = ctrtest::BeanContext::resolveContext("namespace-macro-custom");
    context.discover<^^namespace_macro_fixture>().start();

    auto bean = context.resolve<namespace_macro_fixture::Object>();
    EXPECT_NE(bean.operator->(), nullptr);

    context.stop();
}
