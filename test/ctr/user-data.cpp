#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace user_data_fixture {

struct [[=ctr::session{}]] Service {};

struct Score { int value = 0; };
struct Tag   { int id    = 0; };

} // namespace user_data_fixture

// ─── Acceptance criteria ──────────────────────────────────────────────────────

TEST(UserData, SetThenGetReturnsSameObject) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-basic");
    auto& scope = ctx.resolveScope("s");

    user_data_fixture::Score score{42};
    scope.userData(score);

    auto result = scope.userData<user_data_fixture::Score>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(&result->get(), &score);
    EXPECT_EQ(result->get().value, 42);
    ctx.stop();
}

TEST(UserData, WrongTypeReturnsNullopt) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-wrong-type");
    auto& scope = ctx.resolveScope("s");

    user_data_fixture::Score score{1};
    scope.userData(score);

    // Tag is compatible with nothing — must return nullopt
    auto result = scope.userData<user_data_fixture::Tag>();
    EXPECT_FALSE(result.has_value());
    ctx.stop();
}

TEST(UserData, SetNullptrClearsAssociation) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-nullptr");
    auto& scope = ctx.resolveScope("s");

    user_data_fixture::Score score{7};
    scope.userData(score);
    EXPECT_TRUE(scope.userData<user_data_fixture::Score>().has_value());

    scope.userData(nullptr);
    EXPECT_FALSE(scope.userData<user_data_fixture::Score>().has_value());
    ctx.stop();
}

TEST(UserData, PersistsThroughStopAndStart) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-persist");
    ctx.discover<^^user_data_fixture>();
    auto& scope = ctx.resolveScope("s");
    ctx.start();
    scope.start();

    user_data_fixture::Score score{99};
    scope.userData(score);

    // Readable while scope is running
    ASSERT_TRUE(scope.userData<user_data_fixture::Score>().has_value());
    EXPECT_EQ(scope.userData<user_data_fixture::Score>()->get().value, 99);

    scope.stop();
    // Readable after stop
    ASSERT_TRUE(scope.userData<user_data_fixture::Score>().has_value());
    EXPECT_EQ(scope.userData<user_data_fixture::Score>()->get().value, 99);

    scope.start();
    // Unchanged after restart
    ASSERT_TRUE(scope.userData<user_data_fixture::Score>().has_value());
    EXPECT_EQ(scope.userData<user_data_fixture::Score>()->get().value, 99);

    scope.restart();
    ASSERT_TRUE(scope.userData<user_data_fixture::Score>().has_value());
    EXPECT_EQ(scope.userData<user_data_fixture::Score>()->get().value, 99);

    ctx.stop();
}

TEST(UserData, ConstAccessWorks) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-const");
    auto& scope = ctx.resolveScope("s");

    user_data_fixture::Score score{5};
    scope.userData(score);

    const ctr::ScopedContext& cscope = scope;
    auto result = cscope.userData<user_data_fixture::Score>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().value, 5);
    ctx.stop();
}

TEST(UserData, ReplaceUserData) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-replace");
    auto& scope = ctx.resolveScope("s");

    user_data_fixture::Score score1{1};
    user_data_fixture::Score score2{2};
    scope.userData(score1);
    scope.userData(score2);

    auto result = scope.userData<user_data_fixture::Score>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(&result->get(), &score2);
    ctx.stop();
}

TEST(UserData, NoUserDataReturnsNullopt) {
    auto& ctx   = ctr::BeanContext::resolveContext("ud-none");
    auto& scope = ctx.resolveScope("s");

    EXPECT_FALSE(scope.userData<user_data_fixture::Score>().has_value());
    ctx.stop();
}
