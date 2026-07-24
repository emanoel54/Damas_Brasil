# Makefile para compilar uma aplicação Qt6

# Compilador
CXX = g++

# Nome do executável
TARGET = damas

# Ferramentas Qt
MOC = /usr/lib/qt6/libexec/moc

# Flags de compilação e linkagem (obtidas via pkg-config)
CXXFLAGS = -std=c++17 -fPIC `pkg-config --cflags Qt6Widgets`
LDFLAGS = `pkg-config --libs Qt6Widgets`

# --- Arquivos do Projeto ---

# Fontes C++ escritos manualmente
SOURCES = main.cpp checkerboard.cpp damas_core.cpp

# Cabeçalhos que, quando modificados, devem disparar uma recompilação
HEADERS = checkerboard.h damas_core.hpp

# Fontes gerados pelo MOC
MOC_SOURCES = moc_checkerboard.cpp

# Todos os arquivos objeto necessários
OBJECTS = $(SOURCES:.cpp=.o) $(MOC_SOURCES:.cpp=.o)


# --- Regras de Compilação ---

# A regra 'all' é a padrão, que depende do nosso executável final
.PHONY: all
all: $(TARGET)

# Regra para linkar o executável final.
# $^ representa todos os pré-requisitos (os arquivos .o).
$(TARGET): $(OBJECTS)
	@echo "===> Linkando o executável: $@"
	$(CXX) -o $@ $^ $(LDFLAGS)

# Regra de padrão para compilar fontes C++ em arquivos objeto.
%.o: %.cpp
	@echo "===> Compilando: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regra para gerar o fonte do MOC.
moc_checkerboard.cpp: checkerboard.h
	@echo "===> Gerando MOC: $@"
	$(MOC) $< -o $@

# --- Dependências Adicionais ---
# Garante que os objetos sejam recompilados se seus cabeçalhos mudarem.
main.o: checkerboard.h damas_core.hpp
checkerboard.o: checkerboard.h damas_core.hpp
damas_core.o: damas_core.hpp

# Regra para limpar os arquivos gerados
.PHONY: clean
clean:
	@echo "===> Limpando o projeto..."
	rm -f $(TARGET) $(OBJECTS) $(MOC_SOURCES)
