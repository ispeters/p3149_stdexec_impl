
example: include/* src/*
	clang -isystem ../stdexec/include -std=c++23 -stdlib=libc++ -I include -L/opt/homebrew/opt/llvm/lib/c++ -L/opt/homebrew/opt/llvm/lib/unwind -lunwind -lc++ -o example -g -ftemplate-backtrace-limit=0 src/*.cpp
