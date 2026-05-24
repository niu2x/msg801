.PHONY: build clean

BUILD_DIR := build
CMAKE := cmake

build:
	$(CMAKE) -B $(BUILD_DIR)
	$(CMAKE) --build $(BUILD_DIR)
	$(CMAKE) --install $(BUILD_DIR) --prefix dist

run: build
	./dist/bin/msg801-cli

clean:
	rm -rf $(BUILD_DIR)
