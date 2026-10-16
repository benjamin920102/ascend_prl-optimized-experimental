/* Minimal ACL declarations for the pure-host context test only. */
#ifndef PEARL_TEST_ACL_H
#define PEARL_TEST_ACL_H
using aclError = int;
using aclrtContext = void*;
constexpr aclError ACL_SUCCESS = 0;
constexpr aclError ACL_ERROR_INVALID_PARAM = 100000;
aclError aclrtSetCurrentContext(aclrtContext context);
#endif
