//
// Created by Bart on 4/21/2015.
//

#include "file_system.h"

FileSystem::FileSystem() {
	// Stub
}

FileSystem::~FileSystem() {
	if (main_file) {
		delete[] main_file;
	}

	// Clean up indices
	for (auto &entry : indices) {
		if (entry.second) // We don't see the fs descriptor as an index
			delete entry.second;
	}
}

void FileSystem::load_directory(char *directory) {
	// Do not allow to reload a directory
	if (main_file) {
		//throw std::runtime_error("file system has already been loaded");
	}

	// Does this directory exist... or is it even a directory?
	if (!is_folder(directory)) {
		throw std::runtime_error("directory is invalid or does not exist");
	}

	// Discover main_file_cache.dat2 (the '/' operator glues two paths)
	auto main_file = std::string(directory) + std::string("/") + std::string("main_file_cache.dat2");
	if (!file_exists(const_cast<char*>(main_file.data()))) {
		throw std::runtime_error("filestore does not contain main_file_cache.dat2");
	}

	this->main_file = new char[main_file.length() + 1];
	strcpy(this->main_file, main_file.data());

	// Load all the *.idx files
	for (int index = 0; index <= 255; index++) {
		int buffer_size = strlen(directory) + 1 + strlen("main_file_cache.idx") + 5;
		char *full_path = new char[buffer_size];
		sprintf(full_path, "%s/main_file_cache.idx%d", directory, index);

		// If this index file exists, put it in our library.
		if (file_exists(const_cast<char*>(full_path))) {
			indices.insert(std::make_pair(index, new Index(this, std::string(full_path))));
		}

		delete[] full_path;
	}
}

int FileSystem::get_index_count() {
	auto count = 0;

	for (auto &entry : indices) {
		if (entry.first != 255) // We don't see the fs descriptor as an index
			count++;
	}

	return count;
}

bool FileSystem::has_index(int index) {
	return indices.count(index) != 0;
}

Index *FileSystem::get_index(int directory_id) {
	if (indices.find(directory_id) == indices.end())
		return nullptr;

	return indices.at(directory_id);
}

int FileSystem::read_decompressed(FolderInfo info, vector<char> &dest) {
	vector<char> raw;
	int num_read = read(info, raw);

	// Were we unable to get some data?
	if (num_read == 0)
		return 0;

	// All is good, decompress.
	return Compression::decompress(raw, dest);
}

int FileSystem::read(FolderInfo info, vector<char> &dest) {
	// Make sure this operation isn't going to fail miserably
	if (!info.exists())
		return 0;

	ifstream data_stream(main_file, std::ios_base::binary);
	data_stream.seekg(static_cast<uint64_t>(info.get_offset()), data_stream.beg);

	// Were we able to seek there?
	auto pos = data_stream.tellg();
	if (info.get_offset() != pos)
		return 0;

	// The big format was introduced around revision 667 because the IDs began to exceed 65535.
	bool big_format = info.get_id() > 0xFFFF;
	int header_size = big_format ? 10 : 8;
	int data_size = BLOCK_SIZE - header_size;
	int remaining = info.get_size();

	// Resize the destination buffer to the _presumed_ size. This is to avoid the vector needing to regenerate a buffer each time.
	dest.reserve(remaining);

	char scratch_buffer[BLOCK_SIZE];
	int current_index = 0;
	int current_id = info.get_id();
	int current_part = -1;

	while (remaining > 0) {
		data_stream.read(scratch_buffer, BLOCK_SIZE); // Grab some data to use

		uint32_t folder_id;
		int part_id;
		int next_offset;
		int next_index;

		if (big_format) {
			folder_id = static_cast<uint32_t>(((scratch_buffer[0] & 0xFF) << 24) | ((scratch_buffer[1] & 0xFF) << 16) | ((scratch_buffer[2] & 0xFF) << 8) | (scratch_buffer[3] & 0xFF));
			part_id = ((scratch_buffer[4] & 0xFF) << 8) | (scratch_buffer[5] & 0xFF);
			next_offset = (((scratch_buffer[6] & 0xFF) << 16) | ((scratch_buffer[7] & 0xFF) << 8) | ((scratch_buffer[8]) & 0xFF));
			next_index = scratch_buffer[9] & 0xFF;
		} else {
			folder_id = static_cast<uint32_t>(((scratch_buffer[0] & 0xFF) << 8) | ((scratch_buffer[1]) & 0xFF));
			part_id = ((scratch_buffer[2] & 0xFF) << 8) | (scratch_buffer[3] & 0xFF);
			next_offset = (((scratch_buffer[4] & 0xFF) << 16) | ((scratch_buffer[5] & 0xFF) << 8) | ((scratch_buffer[6]) & 0xFF));
			next_index = scratch_buffer[7] & 0xFF;
		}

		// Check if everything is still OK
		if (folder_id != current_id || part_id != current_part + 1) {
			printf("malformed folder (sequence does not complete: folder %d != %d or part %d != %d)\n", folder_id, current_id, part_id, current_part+1);
			throw std::runtime_error("malformed folder (sequence does not complete)");
		}

		// Fill dest buffer with remaining data
		auto num_left = remaining > data_size ? data_size : remaining;
		dest.insert(dest.end(), &scratch_buffer[header_size], &scratch_buffer[num_left + header_size]);

		// Position us to the next block
		data_stream.seekg(static_cast<uint64_t>(next_offset) * static_cast<uint64_t>(BLOCK_SIZE), data_stream.beg);

		current_part = part_id;
		remaining -= data_size;
	}

	data_stream.close();
	return dest.size();
}
