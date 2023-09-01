// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2011 Guy Blelloch and the PBBS team
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <algorithm>
#include <iostream>

#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/internal/file_map.h"
#include "../bench/parse_command_line.h"
#include "NSGDist.h"

#include "../bench/parse_command_line.h"
#include "types.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

template<typename indexType>
struct edgeRange{

    size_t size(){return edges[0];}

    indexType id(){return id_;}

    edgeRange() : edges(parlay::make_slice<indexType*, indexType*>(nullptr, nullptr)) {}

    edgeRange(indexType* start, indexType* end, indexType id) : edges(parlay::make_slice<indexType*, indexType*>(start,end)), id_(id) {maxDeg = edges.size()-1;}

    indexType operator [] (indexType j){
        if(j > edges[0]){
            std::cout << "ERROR: tried to exceed range" << std::endl;
            abort();
        } else return edges[j+1];
    }

    void append_neighbor(indexType nbh){
        if(edges[0] == maxDeg){
            std::cout << "ERROR in add_neighbor: cannot exceed max degree " << maxDeg << std::endl;
            abort();
        }else{
            edges[edges[0]+1] = nbh;
            edges[0] += 1;
        }
    }

    template<typename rangeType>
    void add_neighbors(rangeType r){
        if(r.size() > maxDeg){
            std::cout << "ERROR in add_neighbors: cannot exceed max degree " << maxDeg << std::endl;
            abort();
        }
        edges[0] = r.size();
        for(int i=0; i<r.size(); i++){
            edges[i+1] = r[i];
        }    
    }

    template<typename rangeType>
    void append_neighbors(rangeType r){
        if(r.size() + edges[0] > maxDeg){
            std::cout << "ERROR in append_neighbors for point " << id_ << ": cannot exceed max degree " << maxDeg << std::endl;
            std::cout << edges[0] << std::endl;
            std::cout << r.size() << std::endl;
            abort();
        }
        for(int i=0; i<r.size(); i++){edges[edges[0]+i+1] = r[i];}
        edges[0] += r.size();
    }

    void clear_neighbors(){
        edges[0]=0;
    }

    void prefetch(){
        int l = ((edges[0]+1) * sizeof(indexType))/64;
        for (int i=0; i < l; i++)
            __builtin_prefetch((char*) edges.begin() + i* 64);
    }

    template<typename F>
    void sort(F&& less){std::sort(edges.begin()+1, edges.begin()+1+edges[0], less);}

    private:
        parlay::slice<indexType*, indexType*> edges;
        long maxDeg;
        indexType id_;
        
};

template<typename indexType>
struct Graph{
    long max_degree(){return maxDeg;}
    size_t size(){return n;}

    Graph(){}

    Graph(long maxDeg, size_t n) : maxDeg(maxDeg), n(n) {
        graph = parlay::sequence<indexType>(n*(maxDeg+1),0);
    }

    // //TODO work in blocks for sake of memory
    Graph(char* gFile){
        auto [fileptr, length] = mmapStringFromFile(gFile);
        indexType num_points = *((indexType*)fileptr);
        indexType max_deg = *((indexType*)(fileptr + sizeof(indexType)));
        n = num_points;
        maxDeg = max_deg;
        std::cout << "Detected " << num_points << " points with max degree " << max_deg << std::endl;
        indexType* degrees_start = (indexType*)(fileptr + 2*sizeof(indexType));
        indexType* degrees_end = degrees_start + num_points;
        parlay::slice<indexType*, indexType*> degrees = parlay::make_slice(degrees_start, degrees_end);
        auto [offsets, total] = parlay::scan(degrees);
        offsets.push_back(total);
        graph = parlay::sequence<indexType>(n*(maxDeg+1),0);
        indexType* edges_start = degrees_end;
        parlay::parallel_for(0, n, [&] (size_t i){
            graph[i*(maxDeg+1)] = degrees[i];
            for(size_t j=0; j<degrees[i]; j++){
                graph[i*(maxDeg+1)+1+j] = *(edges_start + offsets[i] + j);
            }
        });
    }

    //TODO work in blocks for sake of memory
    void save(char* oFile){
        std::cout << "Writing graph with " << n << " points and max degree " << maxDeg
                    << std::endl;
        parlay::sequence<indexType> preamble = {static_cast<indexType>(n), static_cast<indexType>(maxDeg)};
        parlay::sequence<indexType> sizes = parlay::tabulate(n, [&] (size_t i){return static_cast<indexType>((*this)[i].size());});
        parlay::sequence<parlay::sequence<indexType>> edge_data = parlay::tabulate(n, [&] (size_t i){
            return parlay::tabulate(sizes[i], [&] (size_t j){return (*this)[i][j];});
        });
        parlay::sequence<indexType> data = parlay::flatten(edge_data);
        std::ofstream writer;
        writer.open(oFile, std::ios::binary | std::ios::out);
        writer.write((char*)preamble.begin(), 2 * sizeof(indexType));
        writer.write((char*)sizes.begin(), sizes.size() * sizeof(indexType));
        writer.write((char*)data.begin(), data.size() * sizeof(indexType));
        writer.close();

    }

    edgeRange<indexType> operator [] (indexType i) {return edgeRange<indexType>(graph.begin()+i*(maxDeg+1), graph.begin()+(i+1)*(maxDeg+1), i);}

    private:
        size_t n;
        long maxDeg;
        parlay::sequence<indexType> graph;
        
        
};