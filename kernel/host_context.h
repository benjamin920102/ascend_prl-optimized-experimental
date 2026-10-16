#ifndef PEARL_HOST_CONTEXT_H
#define PEARL_HOST_CONTEXT_H

#include "acl/acl.h"

// This miner owns the calling threads' ACL context. Do not switch contexts
// behind this cache; clear it first if another ACL user takes over the thread.
inline aclrtContext& pearl_thread_context()
{
    static thread_local aclrtContext bound = nullptr;
    return bound;
}

inline aclError pearl_bind_context(aclrtContext context)
{
    if (!context) return ACL_ERROR_INVALID_PARAM;
    aclrtContext& bound = pearl_thread_context();
    if (bound == context) return ACL_SUCCESS;
    const aclError error = aclrtSetCurrentContext(context);
    if (error == ACL_SUCCESS) bound = context;
    return error;
}

#endif
