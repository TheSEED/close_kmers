#include <stdlib.h>
#include <unordered_map>
#include <array>
#include <string>
#include <map>
#include <fstream>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "popen.h"

#include "kmer.h"

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
	elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

KmerPegMapping::KmerPegMapping(const std::string &data_dir) :
    data_dir_(data_dir),
    next_genome_id_(0)
{
    std::string genome_file = data_dir_ + "/genomes";
    std::ifstream gfile(genome_file);
    if (gfile.fail())
    {
	std::cout << "Failed to open " << genome_file << "\n";
	exit(1);
    }

    std::string line;
    while (std::getline(gfile, line))
    {
	std::vector<std::string> x = split(line, '\t');
	std::string genome = x[1];
	unsigned long id = next_genome_id_++;
	genome_to_id_[genome] = id;
	id_to_genome_[id] = genome;
	
    }

    gfile.close();
}

void KmerPegMapping::load_mapping_file(const std::string &mapping_file)
{
    /*
     * Read and encode the peg/kmer data.
     */
    unsigned int i = 0;

    std::ifstream kfile(mapping_file);

    std::string line;
    while (std::getline(kfile, line))
    {
	size_t i1 = line.find('\t');
	unsigned long kmer = std::stoul(line.substr(0, i1));
	i1 = line.find('|', i1 + 1);
	size_t i2 = line.find('p', i1 + 1);
	i1 = line.find('.', i2);
	i2 = line.find('\t', i1);

	unsigned long enc = encode_id(line.substr(i1 + 1, i2 - i1 - 2),
				      line.substr(i1 + 1, i2 - i1 - 1));
	
	//printf("k=%lu g=%lu id=%lu enc=%lu\n", kmer, gid, id, enc);
	kmer_to_id_[kmer].push_back(enc);
	i++;
	if (i % 1000000 == 0)
	    printf("%d\n", i);

    }
    kfile.close();
}

void KmerPegMapping::load_compact_mapping_file(const std::string &mapping_file)
{
    /*
     * Read and encode the peg/kmer data.
     */
    unsigned int i = 0;

    if (mapping_file.substr(mapping_file.length() - 3) == ".gz")
    {
	Popen pipe_wrapper(("gunzip < " + mapping_file).c_str());
	io::file_descriptor_source pipe_device(fileno(pipe_wrapper.stream()),
					       io::never_close_handle);
	io::stream<io::file_descriptor_source> pipe_stream(pipe_device, 0x1000, 0x1000);
	load_compact_mapping_file(pipe_stream);
    }
    else
    {
	std::ifstream kfile(mapping_file);
	load_compact_mapping_file(kfile);
	kfile.close();
    }
}

void KmerPegMapping::load_compact_mapping_file(std::istream &kfile)
{
    unsigned long kmer;
    id_set vals;

    while (kfile >> kmer)
    {
	unsigned long size;
	kfile >> size;

	vals.reserve(size);
	
	while (kfile.peek() != '\n')
	{
	    std::string fid;
	    kfile >> fid;

	    encoded_id_t enc = encode_id(fid);
	    vals.push_back(enc);
	}
	// std::cout << kmer << " " << size << " got n values " << vals.size() << "\n";
	kmer_to_id_[kmer] = vals;
	vals.clear();
    }
}

void KmerPegMapping::add_mapping(KmerPegMapping::encoded_id_t enc, unsigned long kmer)
{
    // kmer_to_id_[kmer].push_back(enc);

    auto it = kmer_to_id_.find(kmer);

    if (it == kmer_to_id_.end())
    {
	auto n = kmer_to_id_.emplace(std::make_pair(kmer, id_set()));
	// std::cout << "Alloc new for " << kmer << "\n";
	n.first->second.reserve(512);
	n.first->second.push_back(enc);
    }
    else
    {
	// std::cout << "reuse " << kmer << "\n";
	it->second.push_back(enc);
    }
}

KmerPegMapping::encoded_id_t KmerPegMapping::encode_id(const std::string peg)
{
    size_t i1 = peg.find('|');
    size_t i2 = peg.find('p', i1 + 1);
    std::string genome = peg.substr(i1 + 1, i2 - i1 - 2);
    i1 = peg.find('.', i2);
    std::string fid = peg.substr(i1 + 1);
    // std::cout << "peg='" << peg << "' genome='" << genome << "' id='" << fid << "'\n";
    return encode_id(genome, fid);
}

KmerPegMapping::encoded_id_t KmerPegMapping::encode_id(const std::string &genome, const std::string &peg)
{
    unsigned long gid;
    auto it = genome_to_id_.find(genome);
    if (it == genome_to_id_.end())
    {
	gid = next_genome_id_++;
	genome_to_id_[genome] = gid;
	id_to_genome_[gid] = genome;
    }
    else
    {
	gid = it->second;
    }
    // unsigned long gid = genome_to_id_[genome];

    return (gid << 17) | (std::stoul(peg));
}

std::string KmerPegMapping::decode_id(encoded_id_t id)
{
    std::string g = id_to_genome_[id >> 17];
    unsigned int peg = id & 0x7fff;
    return "fig|" + g + ".peg." + std::to_string((long long) peg);
}
