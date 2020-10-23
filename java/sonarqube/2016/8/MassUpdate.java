/*
 * SonarQube
 * Copyright (C) 2009-2016 SonarSource SA
 * mailto:contact AT sonarsource DOT com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.db.version;

import java.sql.Connection;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.sonar.core.util.ProgressLogger;
import org.sonar.db.Database;

import static com.google.common.base.Preconditions.checkState;

public class MassUpdate {

  @FunctionalInterface
  public interface Handler {
    /**
     * Convert some column values of a given row.
     *
     * @return true if the row must be updated, else false. If false, then the update parameter must not be touched.
     */
    boolean handle(Select.Row row, SqlStatement update) throws SQLException;
  }

  @FunctionalInterface
  public interface MultiHandler {
    /**
     * Convert some column values of a given row.
     *
     * @param updateIndex 0-based
     * @return true if the row must be updated, else false. If false, then the update parameter must not be touched.
     */
    boolean handle(Select.Row row, SqlStatement update, int updateIndex) throws SQLException;
  }

  private final Database db;
  private final Connection readConnection;
  private final Connection writeConnection;
  private final AtomicLong counter = new AtomicLong(0L);
  private final ProgressLogger progress = ProgressLogger.create(getClass(), counter);

  private Select select;
  private List<UpsertImpl> updates = new ArrayList<>(1);

  MassUpdate(Database db, Connection readConnection, Connection writeConnection) {
    this.db = db;
    this.readConnection = readConnection;
    this.writeConnection = writeConnection;
  }

  public SqlStatement select(String sql) throws SQLException {
    this.select = SelectImpl.create(db, readConnection, sql);
    return this.select;
  }

  public MassUpdate update(String sql) throws SQLException {
    this.updates.add(UpsertImpl.create(writeConnection, sql));
    return this;
  }

  public MassUpdate rowPluralName(String s) {
    this.progress.setPluralLabel(s);
    return this;
  }

  public void execute(Handler handler) throws SQLException {
    checkState(select != null && !updates.isEmpty(), "SELECT or UPDATE requests are not defined");
    checkState(updates.size() == 1, "There should be only one update when using a " + Handler.class.getName());

    progress.start();
    try {
      select.scroll(row -> callSingleHandler(handler, updates.iterator().next(), row));
      closeUpdates();

      // log the total number of processed rows
      progress.log();
    } finally {
      progress.stop();
    }
  }

  public void execute(MultiHandler handler) throws SQLException {
    checkState(select != null && !updates.isEmpty(), "SELECT or UPDATE(s) requests are not defined");

    progress.start();
    try {
      select.scroll(row -> callMultiHandler(handler, updates, row));
      closeUpdates();

      // log the total number of processed rows
      progress.log();
    } finally {
      progress.stop();
    }
  }

  private void callSingleHandler(Handler handler, Upsert update, Select.Row row) throws SQLException {
    if (handler.handle(row, update)) {
      update.addBatch();
    }
    counter.getAndIncrement();
  }

  private void callMultiHandler(MultiHandler handler, List<UpsertImpl> updates, Select.Row row) throws SQLException {
    int i = 0;
    for (UpsertImpl update : updates) {
      if (handler.handle(row, update, i)) {
        update.addBatch();
      }
      i++;
    }
    counter.getAndIncrement();
  }

  private void closeUpdates() throws SQLException {
    for (UpsertImpl update : updates) {
      if (update.getBatchCount() > 0L) {
        update.execute().commit();
      }
      update.close();
    }
  }

}
