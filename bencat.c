#include <bencodetools/bencode.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

static int teemode;

/*
 * xread() is the same as the read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
static ssize_t xread(int fd, void *buf, size_t count)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, count);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

static size_t xfwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret;
	size_t written = 0;
	char *writeptr = (char *) ptr;

	while (written < nmemb) {
		ret = fwrite(writeptr, size, nmemb - written, stream);
		if (ret == 0)
			break;
		written += ret;
		writeptr += size * ret;
	}

	return written;
}

static int reallocarray(char **buf, size_t *size)
{
	size_t newsize;
	char *p;
	if (*buf == NULL) {
		newsize = 4096;
	} else {
		newsize = *size;
		if (newsize == 0)
			newsize = 4096;
		if (newsize >= (((size_t) - 1) / 2))
			return -1;
		newsize *= 2;
	}
	p = realloc(*buf, newsize);
	if (p != NULL) {
		*buf = p;
		*size = newsize;
	}
	return (*buf != NULL) ? 0 : -1;
}

static int process_object(const struct bencode *b)
{
	size_t len;
	void *s;
	size_t ret;
	s = ben_print(&len, b);
	if (s == NULL)
		return -1;
	assert(len > 0);
	ret = xfwrite(s, 1, len, stderr);
	free(s);
	return (ret == len) ? 0 : -1;
}

static void shift_buffer(char *buf, size_t *size, size_t off)
{
	assert(off <= *size);
	memmove(buf, buf + off, *size - off);
	*size -= off;
}

static int process(int fd)
{
	char *buf = NULL;
	size_t size = 0;
	size_t alloc = 0;
	ssize_t ret;
	int error;
	int needmore = 1;
	size_t off = 0;
	struct bencode *b;

	while (1) {
		if ((alloc - size) == 0 && reallocarray(&buf, &alloc))
			goto error;

		if (needmore) {
			ret = xread(fd, buf + size, alloc - size);
			if (ret < 0)
				goto error;
			if (ret == 0)
				break;
			size += ret;
			needmore = 0;
		}

		off = 0;
		b = ben_decode2(buf, size, &off, &error);
		if (b == NULL) {
			if (error == BEN_INSUFFICIENT) {
				needmore = 1;
				continue;
			}
			fprintf(stderr, "bencat: Invalid data stream\n");
			goto error;
		}

		assert(off > 0);
		shift_buffer(buf, &size, off);

		ret = process_object(b);
		ben_free(b);
		b = NULL;
		if (ret)
			goto error;
	}

	if (size > 0) {
		fprintf(stderr, "bencat: Incomplete data in stream\n");
		goto error;
	}

	free(buf);
	return 0;

error:
	free(buf);
	return -1;
}

static int xclose(int fd)
{
	while (close(fd)) {
		if (errno == EINTR)
			continue;
		return 1;
	}
	return 0;
}

static int process_files(int i, int argc, char *argv[])
{
	int fd;
	int ret;
	for (; i < argc; i++) {
		fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "bencat: Unable to open %s\n", argv[i]);
			return 1;
		}
		ret = process(fd);
		xclose(fd);
		fd = -1;
		if (ret)
			return ret;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc;) {
		if (argv[i][0] != '-')
			break;
		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
		if (strcmp(argv[i], "-t") == 0 ||
		    strcmp(argv[i], "--tee") == 0) {
			teemode = 1;
			i++;
			continue;
		}
		fprintf(stderr, "bencat: Unknown option: %s\n", argv[i]);
		exit(1);
	}

	if (i < argc)
		return process_files(i, argc, argv);
	else
		return process(0);
}
