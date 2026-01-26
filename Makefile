# Makefile for iommap NIF development

REBAR3 ?= rebar3

.PHONY: all compile test clean dialyzer doc

all: compile

compile:
	$(REBAR3) compile

test: compile
	$(REBAR3) eunit

clean:
	$(REBAR3) clean
	rm -rf _build

dialyzer: compile
	$(REBAR3) dialyzer

doc:
	$(REBAR3) edoc

# Docker-based testing
.PHONY: docker-test docker-test-alpine docker-test-debian docker-build docker-clean

docker-build:
	docker build -t iommap-test-alpine -f Dockerfile .
	docker build -t iommap-test-debian -f Dockerfile.debian .

docker-test-alpine:
	docker build -t iommap-test-alpine -f Dockerfile .
	docker run --rm iommap-test-alpine

docker-test-debian:
	docker build -t iommap-test-debian -f Dockerfile.debian .
	docker run --rm iommap-test-debian

docker-test: docker-test-alpine docker-test-debian
	@echo "All Docker tests passed"

docker-compose-test:
	docker-compose up --build --abort-on-container-exit

docker-clean:
	docker rmi iommap-test-alpine iommap-test-debian 2>/dev/null || true
	docker-compose down --rmi local 2>/dev/null || true

# Local macOS test (run directly)
.PHONY: test-macos
test-macos: compile
	@echo "Running tests on macOS..."
	$(REBAR3) eunit

# Interactive shell for testing
.PHONY: shell
shell: compile
	$(REBAR3) shell
