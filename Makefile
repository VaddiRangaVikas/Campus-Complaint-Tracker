# Campus Complaint Tracker - Makefile
# Compiler: GCC (MSYS2 UCRT64 64-bit recommended)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Ibackend/include -Ibackend/lib -DMG_ARCH=MG_ARCH_WIN32
LIBS ?= -lsqlite3 -lws2_32 -lbcrypt

SRCS = backend/src/main.c \
       backend/src/server.c \
       backend/src/database.c \
       backend/src/auth.c \
       backend/src/algorithms.c \
       backend/src/cJSON.c \
       backend/lib/mongoose.o

TARGET = campus_server.exe

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

backend/lib/mongoose.o: backend/lib/mongoose.c backend/lib/mongoose.h
	$(CC) $(CFLAGS) -c backend/lib/mongoose.c -o $@

test_algorithms: backend/src/algorithms.c backend/tests/test_algorithms.c
	$(CC) $(CFLAGS) $^ -o test_algorithms.exe
	./test_algorithms.exe

test_api: backend/tests/test_api.c backend/src/database.c backend/src/auth.c backend/src/cJSON.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o test_api.exe
	./test_api.exe

clean:
	rm -f campus_server.exe test_algorithms.exe test_api.exe *.o backend/lib/*.o

.PHONY: all test_algorithms test_api clean
