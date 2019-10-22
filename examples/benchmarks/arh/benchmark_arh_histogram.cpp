#ifdef GASNET_EX
#define ARH_BENCHMARK
#include "bcl/containers/experimental/rpc_oneway/arh.hpp"
#include <cassert>
#include "include/cxxopts.hpp"

bool use_agg = false;
size_t agg_size = 0;

struct ThreadObjects {
  std::vector<std::atomic<int>> v;
};

ARH::GlobalObject<ThreadObjects> mObjects;

void histogram_handler(int idx) {
  mObjects.get().v[idx] += 1;
}

void worker() {

  int local_range = 1000;
  int total_range = local_range * (int) ARH::nworkers();
  int num_ops = 100000;
  int total_num_ops = num_ops * (int) ARH::nworkers();
  double duration3 = 0;

  size_t my_rank = ARH::my_worker_local();
  size_t nworkers = ARH::nworkers();

  mObjects.get().v = std::vector<std::atomic<int>>(local_range);

  using rv = decltype(ARH::rpc(size_t(), histogram_handler, int()));
  std::vector<rv> futures;

  ARH::barrier();
  timespec time0, time1, time2;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time0);

  for (int i = 0 ; i < num_ops; i++) {
#ifdef ARH_BENCHMARK
    timespec start, end;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
#endif
    size_t target_rank = lrand48() % nworkers;
    int val = lrand48() % local_range;
    rv f;
    if (use_agg) {
      f = ARH::rpc_agg(target_rank, histogram_handler, val);
    } else {
      f = ARH::rpc(target_rank, histogram_handler, val);
    }
    futures.push_back(std::move(f));
#ifdef ARH_BENCHMARK
    static int step = 0;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
    if (ARH::my_worker_local() == 0) {
      duration3 = update_average(duration3, time2long(time_diff(start, end)), ++step);
    }
#endif
  }

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time1);

  ARH::barrier();

  for (int i = 0 ; i < num_ops; i++) {
    futures[i].wait();
  }

  ARH::barrier();
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time2);

  long duration1 = time2long(time_diff(time0, time1));
  double agg_overhead = (double) duration1 / 1e3 / num_ops * MAX(agg_size, 1);
  double ave_overhead = (double) duration1 / 1e3 / num_ops;
  long duration2 = time2long(time_diff(time0, time2));
  ARH::print("Setting: agg_size = %lu; duration = %.2lf s\n", agg_size, duration2 / 1e9);
  ARH::print("ave_overhead = %.2lf us/op; agg_overhead = %.2lf us/op\n", ave_overhead, agg_overhead);
  ARH::print("Gasnet_ex send: overhead = %.3lf us;\n", ARH::duration0 / 1e3);
  ARH::print("rpc_agg lock-unlock without send: overhead = %.3lf us;\n", ARH::duration1 / 1e3);
  ARH::print("rpc_agg lock-unlock with send: overhead = %.3lf us;\n", ARH::duration2 / 1e3);
  ARH::print("per rpc overhead = %.3lf us;\n", duration3 / 1e3);
  ARH::print("Total throughput = %d op/s\n", (int) (num_ops / (duration1 / 1e9)));
}

int main(int argc, char** argv) {
  // one process per node
  cxxopts::Options options("ARH Benchmark", "Benchmark of ARH system");
  options.add_options()
      ("size", "Aggregation size", cxxopts::value<size_t>())
      ;
  auto result = options.parse(argc, argv);
  agg_size = result["size"].as<size_t>();
  assert(agg_size >= 0);
  use_agg = (agg_size != 0);

  ARH::init(15, 16);
  mObjects.init();
  if (use_agg) {
    agg_size = ARH::set_agg_size(agg_size);
  }

  ARH::run(worker);

  ARH::finalize();
}
#else
#include <iostream>
using namespace std;
int main() {
  cout << "Only run arh test with GASNET_EX" << endl;
  return 0;
}
#endif
