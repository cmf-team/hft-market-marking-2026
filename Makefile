COMPOSE := docker compose -f docker/docker-compose.yml
export UID := $(shell id -u)
export GID := $(shell id -g)

.PHONY: image up down build test run shell clean

image:
	$(COMPOSE) build

up:
	$(COMPOSE) up -d dev

down:
	$(COMPOSE) down

build: up
	$(COMPOSE) exec -T dev bash -c "sudo chown -R $(UID):$(GID) /workspace/build && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build"

test: build
	$(COMPOSE) exec -T dev ctest --test-dir build --output-on-failure

run: build
	$(COMPOSE) exec -T dev ./build/backtester $(ARGS)

shell: up
	$(COMPOSE) exec dev bash

clean:
	$(COMPOSE) exec -T dev rm -rf build/* || true
