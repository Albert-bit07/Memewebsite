#include "recommender.hpp"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#ifdef __linux__
#include <sys/resource.h>
#endif
using Clock=std::chrono::steady_clock;
std::vector<int> full_sort(const Recommender& catalog,const std::vector<float>& query,int k,const std::unordered_set<int>& excluded) {
    std::vector<std::pair<float,int>> scores;
    const auto& vectors=catalog.get_embeddings();
    for (int i=0;i<static_cast<int>(vectors.size());i++) {
        if (excluded.count(i)) continue;
        double dot=0,a=0,b=0;
        for (std::size_t j=0;j<query.size();j++) { dot+=double(query[j])*vectors[i][j];a+=double(query[j])*query[j];b+=double(vectors[i][j])*vectors[i][j]; }
        scores.emplace_back(a==0 || b==0 ? 0.0f : static_cast<float>(dot/(std::sqrt(a)*std::sqrt(b))),i);
    }
    std::sort(scores.begin(),scores.end(),[](const auto& a,const auto& b){return a.first==b.first ? a.second<b.second : a.first>b.first;});
    std::vector<int> result;
    for (int i=0;i<std::min(k,static_cast<int>(scores.size()));i++) result.push_back(scores[i].second);
    return result;
}
int main(int argc, char** argv) {
    std::ofstream samples;
    if (argc > 1) { samples.open(argv[1]); if (!samples) return 1; samples << "items,trial,method,elapsed_ms\n"; }
    auto root=std::filesystem::temp_directory_path()/("meme-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        std::mt19937 generator(20261020);
        std::uniform_real_distribution<float> random(-1,1);
        std::cout<<"# compiler="<<__VERSION__<<"; seed=20261020; dimensions=64; k=20; trials=20; excludes_every_10th_item\n";
        std::cout<<"items,method,median_ms,p95_ms,total_ms,queries_per_second,process_peak_rss_kib\n";
        for (int count:{1000,10000,100000}) {
            nlohmann::json paths=nlohmann::json::array();
            {
                std::ofstream csv(root/"vectors.csv");
                for (int i=0;i<count;i++) {
                    paths.push_back("synthetic-"+std::to_string(i));
                    for(int j=0;j<64;j++) csv<<(j?",":"")<<random(generator);
                    csv<<'\n';
                }
                std::ofstream(root/"paths.json")<<paths;
            }
            Recommender catalog;
            // Keep loader diagnostics out of the CSV stream.
            auto* output=std::cout.rdbuf(std::cerr.rdbuf());
            catalog.load_embeddings((root/"vectors.csv").string(),(root/"paths.json").string());
            std::cout.rdbuf(output);
            std::unordered_set<int> excluded;
            for(int i=0;i<count;i+=10) excluded.insert(i);
            std::vector<double> measurements[2];
            for(int trial=0;trial<21;trial++) {
                std::vector<float> query(64);
                for(auto& value:query) value=random(generator);
                std::vector<int> results[2];
                // Alternate ordering to avoid giving one method all warm-cache runs.
                for(int step=0;step<2;step++) {
                    const int method=(trial+step)%2;
                    auto start=Clock::now();
                    results[method]=method ? catalog.recommend(query,20,excluded) : full_sort(catalog,query,20,excluded);
                    auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
                    if(trial) {
                        measurements[method].push_back(elapsed);
                        if(samples.is_open()) samples << count << ',' << trial << ',' << (method ? "partial_sort" : "full_sort") << ',' << elapsed << '\n';
                    }
                }
                if(results[0]!=results[1]) throw std::runtime_error("Exact ranking mismatch");
            }
            long peak=-1;
#ifdef __linux__
            rusage usage{}; getrusage(RUSAGE_SELF,&usage); peak=usage.ru_maxrss;
#endif
            for(int method=0;method<2;method++) {
                auto times=measurements[method]; double total=0;for(double value:times)total+=value;
                std::sort(times.begin(),times.end());
                std::cout<<count<<','<<(method?"partial_sort":"full_sort")<<','<<(times[9]+times[10])/2<<','<<times[18]<<','<<total<<','<<20000/total<<','<<peak<<'\n';
            }
        }
        std::filesystem::remove_all(root);
    } catch(const std::exception& error) { std::filesystem::remove_all(root);std::cerr<<error.what()<<'\n';return 1; }
}
