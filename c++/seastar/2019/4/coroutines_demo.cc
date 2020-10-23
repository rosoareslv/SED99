/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2019 ScyllaDB Ltd.
 */

#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] () -> seastar::future<> {
        std::cout << "this is a completely useless program\nplease stand by...\n";
        auto f = seastar::parallel_for_each(std::vector<int> { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, [] (int i) -> seastar::future<> {
            co_await seastar::sleep(std::chrono::seconds(i));
            std::cout << i << "\n";
        });

        auto file = co_await seastar::open_file_dma("useless_file.txt", seastar::open_flags::create | seastar::open_flags::wo);
        auto out = seastar::make_file_output_stream(file);
        seastar::sstring str = "nothing to see here, move along now\n";
        co_await out.write(str);
        co_await out.flush();
        co_await out.close();

        co_await std::move(f);
        std::cout << "done\n";
    });
}
