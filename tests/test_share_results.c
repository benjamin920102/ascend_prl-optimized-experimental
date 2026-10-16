#include "../src/pools/pool.h"
#include <assert.h>
const pool_frontend_t POOL = {0};
int main(void) {
    pool_conn_t c = {0}; pool_conn_init(&c, "");
    pool_handle_result(&c, "{\"id\":3,\"result\":true,\"error\":null}");
    assert(shares_acc == 0);
    c.pending[101] = 101; shares_sub = 1;
    pool_handle_result(&c, "{\"id\":101,\"result\":true}");
    pool_handle_result(&c, "{\"id\":101,\"result\":true}");
    assert(shares_acc == 1);
    c.pending[102] = 102;
    pool_handle_result(&c, "{\"id\":102,\"result\":false,\"error\":null}");
    assert(shares_rej == 1);
    c.pending[103] = 103;
    pool_handle_result(&c, "{\"id\":103,\"result\":null,\"error\":[20,\"stale\"]}");
    assert(shares_rej == 2);
    pool_handle_result(&c, "{\"id\":3,\"result\":true}");
    assert(shares_acc == 1);
    return 0;
}
