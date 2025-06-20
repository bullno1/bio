#include "common.h"
#include <bio/file.h>
#include <string.h>

static suite_t file_ = {
	.name = "file",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

BIO_TEST(file_, read_write) {
	bio_file_t file;
	bio_error_t error = { 0 };
	bio_fopen(&file, "testfile", "w+", &error);
	CHECK_NO_ERROR(error);

	const char* message = "hello";
	bio_fwrite(file, message, 1, &error);
	bio_fwrite(file, message + 1, strlen(message) - 1, &error);
	bio_fflush(file, &error);
	CHECK_NO_ERROR(error);

	bio_fseek(file, 0, SEEK_SET, &error);
	CHECK_NO_ERROR(error);

	char buffer[128];
	size_t bytes_read = bio_fread(file, buffer, 1, &error);
	bytes_read += bio_fread(file, buffer + 1, sizeof(buffer) - 1, &error);
	CHECK_NO_ERROR(error);
	CHECK(bytes_read == strlen(message), "Invalid file content");
	CHECK(memcmp(buffer, message, bytes_read) == 0, "Invalid file content");

	bio_fclose(file, &error);
	CHECK_NO_ERROR(error);
}
