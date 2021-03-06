/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
/*======
This file is part of PerconaFT.


Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.

----------------------------------------

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License, version 3,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.
======= */

#ident "Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved."

#include "test.h"

// verify recovery of an update log entry which changes values at keys

static const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD|DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;
uint32_t four_byte_desc = 101;
uint64_t eight_byte_desc = 10101;

static void assert_desc_four (DB* db) {
    assert(db->descriptor->dbt.size == sizeof(four_byte_desc));
    assert(*(uint32_t *)(db->descriptor->dbt.data) == four_byte_desc);
}
static void assert_desc_eight (DB* db) {
    assert(db->descriptor->dbt.size == sizeof(eight_byte_desc));
    assert(*(uint32_t *)(db->descriptor->dbt.data) == eight_byte_desc);
}

bool do_crash;

static void checkpoint_callback_1(void * extra) {
    assert(extra == NULL);
    if (do_crash) {
       toku_hard_crash_on_purpose();
    }
}

static void run_test(void)
{
    DB_ENV *env;
    DB *db;
    DB *db2;
    DB *db3;
    DB_TXN* txn;
    DB_TXN* txn2;
    DB_TXN* txn3;
    DBT desc;

    do_crash = false;
    
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(four_byte_desc);
    desc.data = &four_byte_desc;

    DBT other_desc;
    memset(&other_desc, 0, sizeof(other_desc));
    other_desc.size = sizeof(eight_byte_desc);
    other_desc.data = &eight_byte_desc;

    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    { int chk_r = toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }
    { int chk_r = db_env_create(&env, 0); CKERR(chk_r); }
    db_env_set_checkpoint_callback(checkpoint_callback_1, NULL);
    env->set_errfile(env, stderr);
    { int chk_r = env->open(env, TOKU_TEST_FILENAME, envflags, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }

    IN_TXN_COMMIT(env, NULL, txn_1, 0, {
            { int chk_r = db_create(&db, env, 0); CKERR(chk_r); }
            { int chk_r = db->open(db, txn_1, "foo.db", NULL, DB_BTREE, DB_CREATE, 0666); CKERR(chk_r); }
        });
    IN_TXN_COMMIT(env, NULL, txn_2, 0, {
            { int chk_r = db_create(&db2, env, 0); CKERR(chk_r); }
            { int chk_r = db2->open(db2, txn_2, "foo2.db", NULL, DB_BTREE, DB_CREATE, 0666); CKERR(chk_r); }
            { int chk_r = db2->change_descriptor(db2, txn_2, &other_desc, 0); CKERR(chk_r); }
            assert_desc_eight(db2);
        });
    IN_TXN_COMMIT(env, NULL, txn_3, 0, {
            { int chk_r = db_create(&db3, env, 0); CKERR(chk_r); }
            { int chk_r = db3->open(db3, txn_3, "foo3.db", NULL, DB_BTREE, DB_CREATE, 0666); CKERR(chk_r); }
            { int chk_r = db3->change_descriptor(db3, txn_3, &other_desc, 0); CKERR(chk_r); }
            assert_desc_eight(db3);
        });
    
    { int chk_r = env->txn_begin(env, NULL, &txn, 0); CKERR(chk_r); }
    { int chk_r = db->change_descriptor(db, txn, &desc, 0); CKERR(chk_r); }

    { int chk_r = env->txn_begin(env, NULL, &txn2, 0); CKERR(chk_r); }
    { int chk_r = db2->change_descriptor(db2, txn2, &desc, 0); CKERR(chk_r); }

    { int chk_r = env->txn_begin(env, NULL, &txn3, 0); CKERR(chk_r); }
    { int chk_r = db3->change_descriptor(db3, txn3, &desc, 0); CKERR(chk_r); }

    { int chk_r = env->txn_checkpoint(env,0,0,0); CKERR(chk_r); }
    { int chk_r = txn2->abort(txn2); CKERR(chk_r); }
    { int chk_r = txn->commit(txn,0); CKERR(chk_r); }

    do_crash = true;
    env->txn_checkpoint(env,0,0,0);
}


static void run_recover(void)
{
    DB_ENV *env;
    DB *db;
    DB *db2;
    DB *db3;

    { int chk_r = db_env_create(&env, 0); CKERR(chk_r); }
    env->set_errfile(env, stderr);
    { int chk_r = env->open(env, TOKU_TEST_FILENAME, envflags|DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }

    { int chk_r = db_create(&db, env, 0); CKERR(chk_r); }
    { int chk_r = db->open(db, NULL, "foo.db", NULL, DB_BTREE, DB_AUTO_COMMIT, 0666); CKERR(chk_r); }
    assert_desc_four(db);
    { int chk_r = db->close(db, 0); CKERR(chk_r); }

    { int chk_r = db_create(&db2, env, 0); CKERR(chk_r); }
    { int chk_r = db2->open(db2, NULL, "foo2.db", NULL, DB_BTREE, DB_AUTO_COMMIT, 0666); CKERR(chk_r); }
    assert_desc_eight(db2);
    { int chk_r = db2->close(db2, 0); CKERR(chk_r); }

    { int chk_r = db_create(&db3, env, 0); CKERR(chk_r); }
    { int chk_r = db3->open(db3, NULL, "foo3.db", NULL, DB_BTREE, DB_AUTO_COMMIT, 0666); CKERR(chk_r); }
    assert_desc_eight(db3);
    { int chk_r = db3->close(db3, 0); CKERR(chk_r); }

    { int chk_r = env->close(env, 0); CKERR(chk_r); }
}

static int usage(void)
{
    return 1;
}

int test_main(int argc, char * const argv[])
{
    bool do_test = false;
    bool do_recover = false;

    for (int i = 1; i < argc; i++) {
        char * const arg = argv[i];
        if (strcmp(arg, "-v") == 0) {
            verbose++;
            continue;
        }
        if (strcmp(arg, "-q") == 0) {
            verbose--;
            if (verbose < 0)
                verbose = 0;
            continue;
        }
        if (strcmp(arg, "--test") == 0) {
            do_test = true;
            continue;
        }
        if (strcmp(arg, "--recover") == 0) {
            do_recover = true;
            continue;
        }
        if (strcmp(arg, "--help") == 0) {
            return usage();
        }
    }

    if (do_test) {
        run_test();
    }
    if (do_recover) {
        run_recover();
    }

    return 0;
}
