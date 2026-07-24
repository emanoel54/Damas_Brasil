#ifndef DAMAS_AI_HPP
#define DAMAS_AI_HPP

#include "damas_core.hpp"
#include <vector>
#include <atomic>
#include <mutex>
#include <array>
#include <chrono>

namespace DamasAI {
// Estrutura de Entrada para a Busca
struct Search_Input {
    double time = 1.0; // Tempo em segundos
    int max_depth = 100; // Profundidade máxima para a busca
    void init() { time = 1.0; max_depth = 100; }
};

// Estrutura de Saída da Busca
struct Search_Output {
    DamasCore::GameMove move;
    int score = 0;
    int depth = 0;
    uint64_t node = 0;
    double time_spent = 0.0;
    std::vector<DamasCore::GameMove> pv;

    double time() const { return time_spent; }
};

// Estrutura para um lance no livro de aberturas
struct BookMove {
    DamasCore::GameMove move;
    int weight;
};

// --- API Pública ---

// API Principal de Busca (usada internamente pela ponte de compatibilidade)
void search(Search_Output& so, const DamasCore::BoardState& root_node, const Search_Input& si, int ply_count);

// Funções de controle e debug
void interrupt_search();
void clear_interrupt();
Search_Output get_current_so();
void set_debug_arvore(bool active);
std::string formatar_numero_com_pontos(uint64_t n);
int evaluate_board(const DamasCore::BoardState& pos);
void clear_hash();

// API de Aprendizado
void aprender_com_correcao(const DamasCore::BoardState& estado_bom, const DamasCore::BoardState& estado_ruim);

// --- API do Livro de Aberturas ---
void inicializar_livro();
void adicionar_lance_ao_livro(const DamasCore::BoardState& estado, const DamasCore::GameMove& move, int weight);

// --- Ponte de Compatibilidade com a GUI ---
void inicializar_ia();
void definir_nivel(int nivel);
DamasCore::GameMove encontrar_melhor_lance(const DamasCore::BoardState& estado_atual, int ply_count);
DamasCore::GameMove encontrar_melhor_lance_infinito(const DamasCore::BoardState& estado_atual, int ply_count);


// Constantes de Pontuação Tática
constexpr int SCORE_MATE = 20000;
constexpr int SCORE_INF = 30000;

// Enum para o tipo de nó na Tabela de Transposição
enum TTFlag { TT_EXACT = 0, TT_ALPHA = 1, TT_BETA = 2 };

} // namespace DamasAI

#endif // DAMAS_AI_HPP