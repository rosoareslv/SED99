from __future__ import print_function
from typing import Any, Callable, Dict, List, Tuple, Text
from django.db.models.query import QuerySet
import re
import time

def timed_ddl(db, stmt):
    # type: (Any, str) -> None
    print()
    print(time.asctime())
    print(stmt)
    t = time.time()
    db.execute(stmt)
    delay = time.time() - t
    print('Took %.2fs' % (delay,))

def validate(sql_thingy):
    # type: (str) -> None
    # Do basic validation that table/col name is safe.
    if not re.match('^[a-z][a-z\d_]+$', sql_thingy):
        raise Exception('Invalid SQL object: %s' % (sql_thingy,))

def do_batch_update(db, table, cols, vals, batch_size=10000, sleep=0.1):
    # type: (Any, str, List[str], List[str], int, float) -> None
    validate(table)
    for col in cols:
        validate(col)
    stmt = '''
        UPDATE %s
        SET (%s) = (%s)
        WHERE id >= %%s AND id < %%s
    ''' % (table, ', '.join(cols), ', '.join(['%s'] * len(cols)))
    print(stmt)
    (min_id, max_id) = db.execute("SELECT MIN(id), MAX(id) FROM %s" % (table,))[0]
    if min_id is None:
        return

    print("%s rows need updating" % (max_id - min_id,))
    while min_id <= max_id:
        lower = min_id
        upper = min_id + batch_size
        print('%s about to update range [%s,%s)' % (time.asctime(), lower, upper))
        db.start_transaction()
        params = list(vals) + [lower, upper]
        db.execute(stmt, params=params)
        db.commit_transaction()
        min_id = upper
        time.sleep(sleep)

def add_bool_columns(db, table, cols):
    # type: (Any, str, List[str]) -> None
    validate(table)
    for col in cols:
        validate(col)
    coltype = 'boolean'
    val = 'false'

    stmt = (('ALTER TABLE %s ' % (table,)) +
            ', '.join(['ADD %s %s' % (col, coltype) for col in cols]))
    timed_ddl(db, stmt)

    stmt = (('ALTER TABLE %s ' % (table,)) +
            ', '.join(['ALTER %s SET DEFAULT %s' % (col, val) for col in cols]))
    timed_ddl(db, stmt)

    vals = [val] * len(cols)
    do_batch_update(db, table, cols, vals)

    stmt = 'ANALYZE %s' % (table,)
    timed_ddl(db, stmt)

    stmt = (('ALTER TABLE %s ' % (table,)) +
            ', '.join(['ALTER %s SET NOT NULL' % (col,) for col in cols]))
    timed_ddl(db, stmt)

def create_index_if_not_exist(index_name, table_name, column_string, where_clause):
    # type: (Text, Text, Text, Text) -> Text
    #
    # FUTURE TODO: When we no longer need to support postgres 9.3 for Trusty,
    #              we can use "IF NOT EXISTS", which is part of postgres 9.5
    #              (and which already is supported on Xenial systems).
    stmt = '''
        DO $$
        BEGIN
            IF NOT EXISTS (
                SELECT 1
                FROM pg_class
                where relname = '%s'
                ) THEN
                    CREATE INDEX
                    %s
                    ON %s (%s)
                    %s;
            END IF;
        END$$;
        ''' % (index_name, index_name, table_name, column_string, where_clause)
    return stmt

def act_on_message_ranges(db, orm, tasks, batch_size=5000, sleep=0.5):
    # type: (Any, Dict[str, Any], List[Tuple[Callable[[QuerySet], QuerySet], Callable[[QuerySet], None]]], int , float) -> None
    # tasks should be an array of (filterer, action) tuples
    # where filterer is a function that returns a filtered QuerySet
    # and action is a function that acts on a QuerySet

    all_objects = orm['zerver.Message'].objects

    try:
        min_id = all_objects.all().order_by('id')[0].id
    except IndexError:
        print('There is no work to do')
        return

    max_id = all_objects.all().order_by('-id')[0].id
    print("max_id = %d" % (max_id,))
    overhead = int((max_id + 1 - min_id) / batch_size * sleep / 60)
    print("Expect this to take at least %d minutes, just due to sleeps alone." % (overhead,))

    while min_id <= max_id:
        lower = min_id
        upper = min_id + batch_size - 1
        if upper > max_id:
            upper = max_id

        print('%s about to update range %s to %s' % (time.asctime(), lower, upper))

        db.start_transaction()
        for filterer, action in tasks:
            objects = all_objects.filter(id__range=(lower, upper))
            targets = filterer(objects)
            action(targets)
        db.commit_transaction()

        min_id = upper + 1
        time.sleep(sleep)
