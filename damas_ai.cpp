#include "damas_ai.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <zstd.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <random>

namespace DamasAI {

std::atomic<bool> g_debug_arvore{false};

struct TTEntry {
    uint64_t hash;
    uint16_t move; // Compactado: Bits 0-5 (origem), Bits 6-11 (destino). 0xFFFF para vazio.
    int16_t score;
    int8_t depth;
    uint8_t flag;
};

class EGTB {
public:
    static const uint16_t VAL_UNKNOWN = 0x0000;
    static const uint16_t VAL_WIN     = 0x0001; 
    static const uint16_t VAL_LOSS    = 0x0002;
    static const uint16_t VAL_DRAW    = 0x0003;
    static const uint16_t MASK_RES    = 0x0003;

    static uint16_t probe(const DamasCore::BoardState& pos, int& dtm) {
        if (!initialized) init_tables();

        uint32_t wk = convert_to_32(pos.obter_brancas_damas());
        uint32_t wp = convert_to_32(pos.obter_brancas_peoes());
        uint32_t bk = convert_to_32(pos.obter_pretas_damas());
        uint32_t bp = convert_to_32(pos.obter_pretas_peoes());
        int turn = (pos.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? 0 : 1;

        int nwk = __builtin_popcount(wk);
        int nwp = __builtin_popcount(wp);
        int nbk = __builtin_popcount(bk);
        int nbp = __builtin_popcount(bp);

        if (turn == 0) { 
            if (nwk + nwp == 0) { dtm = 0; return VAL_LOSS; }
            if (nbk + nbp == 0) { dtm = 0; return VAL_WIN; }
        } else {
            if (nbk + nbp == 0) { dtm = 0; return VAL_LOSS; }
            if (nwk + nwp == 0) { dtm = 0; return VAL_WIN; }
        }

        if ((nwk + nwp) < (nbk + nbp) || ((nwk + nwp) == (nbk + nbp) && nwk < nbk)) {
            uint32_t twk = reflect_v(bk), twp = reflect_v(bp);
            uint32_t tbk = reflect_v(wk), tbp = reflect_v(wp);
            wk = twk; wp = twp; bk = tbk; bp = tbp;
            std::swap(nwk, nbk); std::swap(nwp, nbp);
            turn = 1 - turn;
        }

        uint32_t key_int = (nwk << 12) | (nwp << 8) | (nbk << 4) | nbp;
        
        std::vector<uint16_t>* v_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            
            auto it = cache.find(key_int);
            if (it == cache.end()) {
                // Previne estouro de memória garantindo apenas N bases carregadas na RAM
                int loaded = 0;
                for (const auto& pair : cache) { if (!pair.second.empty()) loaded++; }
                if (loaded >= 16) {
                    for (auto iter = cache.begin(); iter != cache.end(); ) {
                        if (!iter->second.empty()) iter = cache.erase(iter);
                        else ++iter; // Mantém vetores vazios na memória para evitar releituras síncronas de I/O em disco
                    }
                }

                std::string key_str = std::to_string(nwk) + std::to_string(nwp) + std::to_string(nbk) + std::to_string(nbp);
                std::string path = get_db_path(key_str);
                if (path.empty()) {
                    cache[key_int] = std::vector<uint16_t>();
                } else {
                    std::ifstream f(path, std::ios::binary);
                    if (!f) {
                        cache[key_int] = std::vector<uint16_t>();
                    } else {
                        f.seekg(0, std::ios::end);
                        size_t compressed_size = f.tellg();
                        f.seekg(0, std::ios::beg);
                        std::vector<char> compressed_buffer(compressed_size);
                        f.read(compressed_buffer.data(), compressed_size);
                        f.close();

                        uint64_t c = C[32][nwk] * C[32 - nwk][nwp] * C[32 - nwk - nwp][nbk] * C[32 - nwk - nwp - nbk][nbp];
                        size_t const decompressed_size = c * 2 * sizeof(uint16_t);
                        std::vector<uint16_t> v_data(c * 2);
                        size_t const dSize = ZSTD_decompress(v_data.data(), decompressed_size, compressed_buffer.data(), compressed_size);

                        if (ZSTD_isError(dSize) || dSize != decompressed_size) {
                            cache[key_int] = std::vector<uint16_t>();
                        } else {
                            cache[key_int] = std::move(v_data);
                        }
                    }
                }
                v_ptr = &cache[key_int];
            } else {
                v_ptr = &it->second;
            }
        }

        if (v_ptr->empty()) return VAL_UNKNOWN;

        uint32_t m = 0xFFFFFFFF;
        uint64_t r_wk = rank_masked(wk, nwk, m); m &= ~wk;
        uint64_t r_wp = rank_masked(wp, nwp, m); m &= ~wp;
        uint64_t r_bk = rank_masked(bk, nbk, m); m &= ~bk;
        uint64_t r_bp = rank_masked(bp, nbp, m);
        
        uint64_t c_total = C[32][nwk] * C[32 - nwk][nwp] * C[32 - nwk - nwp][nbk] * C[32 - nwk - nwp - nbk][nbp];
        uint64_t idx = r_wk;
        idx = idx * C[32 - nwk][nwp] + r_wp;
        idx = idx * C[32 - nwk - nwp][nbk] + r_bk;
        idx = idx * C[32 - nwk - nwp - nbk][nbp] + r_bp;
        uint64_t final_idx = idx + (static_cast<uint64_t>(turn) * c_total);

        uint16_t v = (*v_ptr)[final_idx];
        uint16_t res = v & MASK_RES;
        dtm = v >> 2;
        return res;
    }

    static bool probe_with_move(const DamasCore::BoardState& pos, DamasCore::GameMove& best_move, int& score, int& dtm) {
        uint16_t root_res = probe(pos, dtm);
        if (root_res == VAL_UNKNOWN) return false;

        if (root_res == VAL_WIN) score = SCORE_MATE - dtm;
        else if (root_res == VAL_LOSS) score = -SCORE_MATE + dtm;
        else score = 0;

        DamasCore::MoveList moves = DamasCore::gerar_todas_capturas_maximais(pos);
        if (moves.empty()) {
            DamasCore::BoardBitboard pieces = (pos.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? pos.obter_todas_pecas_brancas() : pos.obter_todas_pecas_pretas();
            while(pieces) {
                int sq = __builtin_ctzll(pieces);
                bool is_pawn = ((pos.obter_brancas_peoes() | pos.obter_pretas_peoes()) & (1ULL << sq));
                DamasCore::BoardBitboard dests = is_pawn ? DamasCore::gerar_movimentos_simples_peao(pos, sq) : DamasCore::gerar_movimentos_simples_dama(pos, sq);
                while(dests) { moves.push_back({sq, __builtin_ctzll(dests), 0}); dests &= dests - 1; }
                pieces &= pieces - 1;
            }
        }

        if (moves.empty()) return false;
        if (moves.size() == 1) {
            best_move = moves[0];
            return true;
        }

        DamasCore::GameMove best = {};
        best.casa_origem = -1; // Marca como inválido
        int best_dtm_win = 32000;
        int worst_dtm_loss = -1;

        for (size_t i = 0; i < moves.size(); ++i) {
            DamasCore::BoardState next_pos = pos;
            if (moves[i].mascaras_pecas_capturadas != 0) next_pos.aplicar_movimento_completo_captura(moves[i]);
            else next_pos.aplicar_mov_simples(moves[i].casa_origem, moves[i].casa_destino);
            next_pos.alternar_turno();

            int next_dtm = 0;
            uint16_t v_res = probe(next_pos, next_dtm);
            if (v_res == VAL_UNKNOWN) continue;

            if (root_res == VAL_WIN) {
                if (v_res == VAL_LOSS) { // Oponente perde, este é um lance vencedor para nós
                    if (best.casa_origem == -1 || next_dtm < best_dtm_win) {
                        best = moves[i];
                        best_dtm_win = next_dtm;
                    }
                }
            } else if (root_res == VAL_LOSS) {
                if (v_res == VAL_WIN) { // Oponente ganha, queremos atrasar a derrota
                    if (best.casa_origem == -1 || next_dtm > worst_dtm_loss) {
                        best = moves[i];
                        worst_dtm_loss = next_dtm;
                    }
                }
            } else { // EMPATE
                if (v_res == VAL_DRAW) {
                    best = moves[i];
                    break; // Qualquer lance de empate serve
                }
            }
        }

        if (best.casa_origem != -1) { best_move = best; return true; }
        return false;
    }

private:
    static std::unordered_map<uint32_t, std::vector<uint16_t>> cache;
    static std::mutex cache_mutex;
    static uint64_t C[33][33];
    static bool initialized;

    static void init_tables() {
        for (int n = 0; n <= 32; n++) {
            C[n][0] = 1;
            for (int k = 1; k <= n; k++) C[n][k] = (n > 0) ? C[n-1][k-1] + C[n-1][k] : 0;
        }
        initialized = true;
    }

    static uint32_t reflect_v(uint32_t b) {
        uint32_t res = 0;
        for (int i = 0; i < 32; i++) {
            if ((b >> i) & 1) {
                int r = (i / 4) + 1;
                int c = (i % 4) * 2 + ((r % 2 == 0) ? 2 : 1);
                int nr = 9 - r;
                int nc = 9 - c;
                int nsq = (nr - 1) * 4 + (nc - 1) / 2;
                res |= (1U << nsq);
            }
        }
        return res;
    }

    static uint64_t rank_masked(uint32_t subset, int k, uint32_t mask) {
        if (k <= 0) return 0;
        uint64_t r = 0; int n = __builtin_popcount(mask);
        for (int i = 31; i >= 0 && k > 0; i--) {
            if ((mask >> i) & 1) { 
                if ((subset >> i) & 1) { r += C[n - 1][k]; k--; } 
                n--; 
            }
        }
        return r;
    }

    static uint32_t convert_to_32(uint64_t bb64) {
        uint32_t bb32 = 0;
        while (bb64) {
            int sq64 = __builtin_ctzll(bb64);
            int linha = sq64 / 8;
            int coluna = sq64 % 8;
            int r = 8 - linha;
            int c = coluna + 1;
            int sq32 = (r - 1) * 4 + (c - 1) / 2;
            bb32 |= (1U << sq32);
            bb64 &= bb64 - 1;
        }
        return bb32;
    }

    static std::string get_db_path(const std::string& key) {
        std::error_code ec_sym;
        std::filesystem::path exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec_sym).parent_path();
        
        if (!ec_sym && std::filesystem::exists(exe_dir / "db" / (key + ".lbnz"))) {
            return (exe_dir / "db" / (key + ".lbnz")).string();
        }
        if (std::filesystem::exists("db/" + key + ".lbnz")) return "db/" + key + ".lbnz";
        if (std::filesystem::exists("../db/" + key + ".lbnz")) return "../db/" + key + ".lbnz";
        return "";
    }
};

std::unordered_map<uint32_t, std::vector<uint16_t>> EGTB::cache;
std::mutex EGTB::cache_mutex;
uint64_t EGTB::C[33][33];
bool EGTB::initialized = false;

// O coração do motor escondido no .cpp para manter a API limpa
class DamasEngine {
    // Estrutura para dados específicos de cada thread, evitando corridas de dados.
    struct ThreadData {
        uint64_t rep_stack[1024];
        DamasCore::GameMove killers[128][2];
        int history[64][64];

        void clear_state() {
            for(int r=0; r<64; ++r) for(int c=0; c<64; ++c) history[r][c] = 0;
            for(int i=0; i<128; ++i) { killers[i][0] = {}; killers[i][1] = {}; }
        }
    };

public:
    DamasEngine() : _tt(TT_SIZE) {
        // Usa o número de núcleos de CPU disponíveis, com um mínimo de 1.
        _num_threads = std::max(1u, std::thread::hardware_concurrency());
        _thread_data.resize(_num_threads);
        clear_state();
    }

    void search(Search_Output& so, const DamasCore::BoardState& root, const Search_Input& si, int ply_count);
    void interrupt() { _stop = true; }
    void clear_interrupt() { _stop = false; }
    void clear_hash() { 
        // NOTA: A limpeza do hash é global e afeta todas as threads.
        std::memset(_tt.data(), 0, sizeof(TTEntry) * TT_SIZE); 
        clear_state(); // Apaga as heurísticas de ordenação (killers e history) também!
    }
    int evaluate(const DamasCore::BoardState& pos);
    Search_Output get_current_so() { 
        std::lock_guard<std::mutex> lock(_so_mutex); 
        Search_Output out = _current_so; // _current_so holds the last completed iteration's best move, score, and depth.
        out.node = _nodes.load(std::memory_order_relaxed); // Always get the most current node count.
        out.time_spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start_time).count(); // Always get the most current time spent.
        return out; 
    }

private:
    static constexpr size_t TT_SIZE = 1 << 22; // ~4M entradas
    std::vector<TTEntry> _tt; // Tabela de transposição, compartilhada entre threads.

    unsigned int _num_threads;
    std::vector<ThreadData> _thread_data; // Dados por thread.

    std::atomic<bool> _stop{false};
    std::atomic<uint64_t> _nodes{0};
    std::chrono::time_point<std::chrono::steady_clock> _start_time;
    double _time_limit = 1.0;
    std::mutex _so_mutex;
    Search_Output _current_so;

    void clear_state();
    inline void check_time();
    int quiescence(DamasCore::BoardState pos, int alpha, int beta, int q_ply, unsigned int thread_id);
    int alpha_beta(DamasCore::BoardState pos, int depth, int alpha, int beta, int ply, bool do_null, unsigned int thread_id);
    void sort_moves(DamasCore::MoveList& moves, uint16_t tt_move_compact, int ply, unsigned int thread_id);
    void extract_pv(DamasCore::BoardState pos, std::vector<DamasCore::GameMove>& pv);
};

static DamasEngine EngineGlobal;

// Lista com os nomes das regras para salvar no arquivo e facilitar a leitura
static const char* NOME_REGRAS_IA[18] = {
    "Material Basico (Peoes e Damas)",
    "Defesa da Fileira de Coroacao (Back Rank)",
    "Peoes Apoiados (Diagonais conectadas)",
    "Pedra Cao (Outposts em c5/f6/c3/f4)",
    "Controle do Centro Expansivo",
    "Controle do Carreirao Principal (a1-h8)",
    "Peoes nas Bordas (Seguros mas sem dominio)",
    "Avanco de Peoes (Incentivo direcional)",
    "Especialistas (Armadilhas e prisoes)",
    "Balanceamento dos Flancos",
    "Estrutura de Defesa Forte do Fundo",
    "Dominio do Centro Absoluto",
    "Penalidade de Estruturas Fracas",
    "Combate aos Postos Inimigos",
    "Desenvolvimento do Carreirao Inferior",
    "Penalidade de Avanco Prematuro (5a fileira)",
    "Mobilidade e Liberdade de Movimentos",
    "Escudos da Base (Triangulos de Protecao)"
};

// --- Variáveis para Aprendizado de Máquina (Pesos DXP) ---
static std::vector<int> g_pesos_aprendizado;
static std::unordered_set<uint64_t> g_padroes_ruins; // Memória fotográfica de erros
static std::unordered_set<uint64_t> g_padroes_bons;  // Memória fotográfica de acertos
static bool g_pesos_carregados = false;

// --- Variáveis para o Livro de Aberturas ---
static std::unordered_map<uint64_t, std::vector<BookMove>> g_livro_aberturas;
static bool g_livro_carregado = false;

// Função Auxiliar Robusta para Obter o Diretório de Dados (Homogeneizada)
static std::string get_data_dir() {
    std::error_code ec_sym;
    std::filesystem::path exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec_sym).parent_path();
    
    if (!ec_sym && std::filesystem::exists(exe_dir / "data")) {
        return (exe_dir / "data").string();
    } else if (std::filesystem::exists("data")) {
        return "data";
    } else if (std::filesystem::exists("../data")) {
        return "../data";
    } else if (std::filesystem::exists("/opt/lib-engine/data")) {
        return "/opt/lib-engine/data";
    } else {
        std::error_code ec;
        if (!ec_sym && !exe_dir.empty()) {
            std::filesystem::create_directories(exe_dir / "data", ec);
            return (exe_dir / "data").string();
        } else {
            std::filesystem::create_directory("data", ec);
            return "data";
        }
    }
}

static void carregar_padroes_memoria() {
    std::string dir = get_data_dir();
    
    std::string path_ruins = dir + "/padroes_ruins.bin";
    std::ifstream fin_ruins(path_ruins, std::ios::binary);
    if (fin_ruins.is_open()) {
        g_padroes_ruins.clear();
        uint64_t h;
        while (fin_ruins.read(reinterpret_cast<char*>(&h), sizeof(h))) {
            g_padroes_ruins.insert(h);
        }
        fin_ruins.close();
    } else {
        // Migração automática do formato .txt antigo
        std::string path_ruins_txt = dir + "/padroes_ruins.txt";
        std::ifstream fin_ruins_txt(path_ruins_txt);
        if (fin_ruins_txt.is_open()) {
            g_padroes_ruins.clear();
            uint64_t h;
            while (fin_ruins_txt >> h) g_padroes_ruins.insert(h);
            fin_ruins_txt.close();
        }
    }

    std::string path_bons = dir + "/padroes_bons.bin";
    std::ifstream fin_bons(path_bons, std::ios::binary);
    if (fin_bons.is_open()) {
        g_padroes_bons.clear();
        uint64_t h;
        while (fin_bons.read(reinterpret_cast<char*>(&h), sizeof(h))) {
            g_padroes_bons.insert(h);
        }
        fin_bons.close();
    } else {
        // Migração automática do formato .txt antigo
        std::string path_bons_txt = dir + "/padroes_bons.txt";
        std::ifstream fin_bons_txt(path_bons_txt);
        if (fin_bons_txt.is_open()) {
            g_padroes_bons.clear();
            uint64_t h;
            while (fin_bons_txt >> h) g_padroes_bons.insert(h);
            fin_bons_txt.close();
        }
    }
}

static void carregar_pesos_ia() {
    std::string path = get_data_dir() + "/pesos_tabela.bin";
    std::ifstream fin(path, std::ios::binary);
    
    g_pesos_aprendizado.clear();
    
    if (fin.is_open()) {
        int p;
        while (fin.read(reinterpret_cast<char*>(&p), sizeof(p))) {
            g_pesos_aprendizado.push_back(p);
        }
        fin.close();
    } else {
        // Migração automática do formato .txt antigo
        std::string path_txt = get_data_dir() + "/pesos_tabela.txt";
        std::ifstream fin_txt(path_txt);
        if (fin_txt.is_open()) {
            std::string linha;
            while (std::getline(fin_txt, linha)) {
                std::stringstream ss(linha);
                int p;
                if (ss >> p) g_pesos_aprendizado.push_back(p);
            }
            fin_txt.close();
        }
    }

    // Escudo Protetor: Garante que o vetor tenha sempre os 18 pesos
    while (g_pesos_aprendizado.size() < 18) {
        g_pesos_aprendizado.push_back(100);
    }

    std::ofstream fout(path, std::ios::binary);
    if (fout.is_open()) {
        for (size_t i = 0; i < 18; i++) {
            int p = g_pesos_aprendizado[i];
            fout.write(reinterpret_cast<char*>(&p), sizeof(p));
        }
        fout.close();
    }

    g_pesos_carregados = true;
    carregar_padroes_memoria();
}

static void salvar_livro() {
    if (!g_livro_carregado) return;

    std::string path = get_data_dir() + "/book.bin";
    std::ofstream fout(path, std::ios::binary | std::ios::trunc);
    if (!fout.is_open()) return;

    uint64_t num_positions = g_livro_aberturas.size();
    fout.write(reinterpret_cast<const char*>(&num_positions), sizeof(num_positions));

    for (const auto& pair : g_livro_aberturas) {
        uint64_t hash = pair.first;
        const auto& moves = pair.second;
        int num_moves = moves.size();

        fout.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        fout.write(reinterpret_cast<const char*>(&num_moves), sizeof(num_moves));

        for (const auto& book_move : moves) {
            fout.write(reinterpret_cast<const char*>(&book_move.move.casa_origem), sizeof(book_move.move.casa_origem));
            fout.write(reinterpret_cast<const char*>(&book_move.move.casa_destino), sizeof(book_move.move.casa_destino));
            fout.write(reinterpret_cast<const char*>(&book_move.move.mascaras_pecas_capturadas), sizeof(book_move.move.mascaras_pecas_capturadas));
            fout.write(reinterpret_cast<const char*>(&book_move.weight), sizeof(book_move.weight));
        }
    }
    fout.close();
}

void inicializar_livro() {
    std::string path = get_data_dir() + "/book.bin";
    std::ifstream fin(path, std::ios::binary);

    g_livro_aberturas.clear();

    if (fin.is_open()) {
        uint64_t num_positions;
        fin.read(reinterpret_cast<char*>(&num_positions), sizeof(num_positions));
        if (fin.fail()) { g_livro_carregado = true; return; }

        for (uint64_t i = 0; i < num_positions; ++i) {
            uint64_t hash;
            int num_moves;
            fin.read(reinterpret_cast<char*>(&hash), sizeof(hash));
            fin.read(reinterpret_cast<char*>(&num_moves), sizeof(num_moves));
            if (fin.fail()) break;

            std::vector<BookMove> moves;
            for (int j = 0; j < num_moves; ++j) {
                BookMove book_move;
                fin.read(reinterpret_cast<char*>(&book_move.move.casa_origem), sizeof(book_move.move.casa_origem));
                fin.read(reinterpret_cast<char*>(&book_move.move.casa_destino), sizeof(book_move.move.casa_destino));
                fin.read(reinterpret_cast<char*>(&book_move.move.mascaras_pecas_capturadas), sizeof(book_move.move.mascaras_pecas_capturadas));
                fin.read(reinterpret_cast<char*>(&book_move.weight), sizeof(book_move.weight));
                if (fin.fail()) break;
                moves.push_back(book_move);
            }
            g_livro_aberturas[hash] = moves;
        }
        fin.close();
    }
    g_livro_carregado = true;
}

void adicionar_lance_ao_livro(const DamasCore::BoardState& estado, const DamasCore::GameMove& move, int weight) {
    if (!g_livro_carregado) inicializar_livro();

    uint64_t hash = estado.obter_hash();
    auto& moves_for_pos = g_livro_aberturas[hash];

    bool found = false;
    for (auto& book_move : moves_for_pos) {
        if (book_move.move == move) {
            book_move.weight = weight;
            found = true;
            break;
        }
    }

    if (!found) {
        moves_for_pos.push_back({move, weight});
    }

    moves_for_pos.erase(
        std::remove_if(moves_for_pos.begin(), moves_for_pos.end(), [](const BookMove& bm){ return bm.weight <= 0; }),
        moves_for_pos.end()
    );

    if (moves_for_pos.empty()) {
        g_livro_aberturas.erase(hash);
    }

    salvar_livro();
}

// --- Implementação da API Externa ---
void search(Search_Output& so, const DamasCore::BoardState& root_node, const Search_Input& si, int ply_count) { EngineGlobal.search(so, root_node, si, ply_count); }
void interrupt_search() { EngineGlobal.interrupt(); }
void clear_interrupt() { EngineGlobal.clear_interrupt(); }
Search_Output get_current_so() { return EngineGlobal.get_current_so(); }
void clear_hash() { EngineGlobal.clear_hash(); }

// Formata um número inteiro grande para uma string com separadores de milhar.
// Exemplo: 1234567 -> "1.234.567"
std::string formatar_numero_com_pontos(uint64_t n) {
    if (n == 0) {
        return "0";
    }

    std::string numero_formatado;
    std::string s = std::to_string(n);
    
    int count = 0;
    for (int i = s.length() - 1; i >= 0; i--) {
        numero_formatado += s[i];
        count++;
        if (count == 3 && i > 0) {
            numero_formatado += '.';
            count = 0;
        }
    }
    
    std::reverse(numero_formatado.begin(), numero_formatado.end());
    return numero_formatado;
}

void set_debug_arvore(bool active) { g_debug_arvore = active; }
int evaluate_board(const DamasCore::BoardState& pos) { return EngineGlobal.evaluate(pos); }

// --- Métodos do Motor Interno ---
void DamasEngine::clear_state() {
    for (auto& data : _thread_data) {
        data.clear_state();
    }
}

inline void DamasEngine::check_time() {
    // O contador de nós é atômico. A verificação de tempo ocorre a cada 2048 nós
    // para reduzir a sobrecarga, e pode ser feita por qualquer thread que
    // aconteça de atingir o nó de número múltiplo de 2048.
    if ((_nodes.fetch_add(1, std::memory_order_relaxed) & 2047) == 0) {
        auto elapsed = std::chrono::steady_clock::now() - _start_time;
        if (std::chrono::duration<double>(elapsed).count() >= _time_limit) _stop = true;
    }
}

DamasCore::GameMove consultar_livro(const DamasCore::BoardState& estado, int ply_count) {
    DamasCore::GameMove invalid_move = {};
    invalid_move.casa_origem = -1;

    if (!g_livro_carregado || ply_count >= 10) {
        return invalid_move;
    }

    uint64_t hash = estado.obter_hash();
    auto it = g_livro_aberturas.find(hash);

    if (it == g_livro_aberturas.end() || it->second.empty()) {
        return invalid_move;
    }

    const auto& moves = it->second;
    int total_weight = 0;
    for (const auto& book_move : moves) {
        total_weight += book_move.weight;
    }

    if (total_weight <= 0) {
        return invalid_move;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, total_weight);
    int random_pick = distrib(gen);

    int current_weight_sum = 0;
    for (const auto& book_move : moves) {
        current_weight_sum += book_move.weight;
        if (random_pick <= current_weight_sum) {
            return book_move.move;
        }
    }

    return invalid_move;
}

// Disseca a avaliação sem os pesos, para sabermos o que a IA pensou em cada parâmetro separadamente
std::array<int, 18> extrair_componentes_avaliacao(const DamasCore::BoardState& pos) {
    std::array<int, 18> comp;
    comp.fill(0);
    const DamasCore::BoardBitboard wp = pos.obter_brancas_peoes();
    const DamasCore::BoardBitboard bp = pos.obter_pretas_peoes();
    const DamasCore::BoardBitboard wk = pos.obter_brancas_damas();
    const DamasCore::BoardBitboard bk = pos.obter_pretas_damas();

    comp[0] += (DamasCore::contar_bits_ativos(wp) - DamasCore::contar_bits_ativos(bp)) * 100;
    comp[0] += (DamasCore::contar_bits_ativos(wk) - DamasCore::contar_bits_ativos(bk)) * 400;
    
    comp[1] += DamasCore::contar_bits_ativos(wp & DamasCore::RANK_PRIMEIRA_PRETA) * 15;
    comp[1] -= DamasCore::contar_bits_ativos(bp & DamasCore::RANK_ULTIMA_BRANCA) * 15;

    DamasCore::BoardBitboard supported_wp = wp & (((wp & DamasCore::NAO_COLUNA_A) >> 9) | ((wp & DamasCore::NAO_COLUNA_H) >> 7));
    DamasCore::BoardBitboard supported_bp = bp & (((bp & DamasCore::NAO_COLUNA_A) << 7) | ((bp & DamasCore::NAO_COLUNA_H) << 9));
    comp[2] += DamasCore::contar_bits_ativos(supported_wp) * 12;
    comp[2] -= DamasCore::contar_bits_ativos(supported_bp) * 12;

    const DamasCore::BoardBitboard WHITE_OUTPOSTS = (1ULL << 21) | (1ULL << 26);
    comp[3] += DamasCore::contar_bits_ativos(supported_wp & WHITE_OUTPOSTS) * 30;
    const DamasCore::BoardBitboard BLACK_OUTPOSTS = (1ULL << 37) | (1ULL << 42);
    comp[3] -= DamasCore::contar_bits_ativos(supported_bp & BLACK_OUTPOSTS) * 30;

    const DamasCore::BoardBitboard CENTER = 0x00003C3C3C3C0000ULL;
    comp[4] += DamasCore::contar_bits_ativos(wp & CENTER) * 10 + DamasCore::contar_bits_ativos(wk & CENTER) * 25;
    comp[4] -= DamasCore::contar_bits_ativos(bp & CENTER) * 10 + DamasCore::contar_bits_ativos(bk & CENTER) * 25;

    const DamasCore::BoardBitboard CARREIRAO = (1ULL<<56) | (1ULL<<49) | (1ULL<<42) | (1ULL<<35) | (1ULL<<28) | (1ULL<<21) | (1ULL<<14) | (1ULL<<7);
    comp[5] += DamasCore::contar_bits_ativos(wp & CARREIRAO) * 4 + DamasCore::contar_bits_ativos(wk & CARREIRAO) * 25;
    comp[5] -= DamasCore::contar_bits_ativos(bp & CARREIRAO) * 4 + DamasCore::contar_bits_ativos(bk & CARREIRAO) * 25;

    const DamasCore::BoardBitboard BORDAS = DamasCore::BITBOARD_COLUNA_A | DamasCore::BITBOARD_COLUNA_H;
    comp[6] += DamasCore::contar_bits_ativos(wp & BORDAS) * 3;
    comp[6] -= DamasCore::contar_bits_ativos(bp & BORDAS) * 3;

    comp[7] += DamasCore::contar_bits_ativos(wp & 0x000000000000FF00ULL) * 5 + DamasCore::contar_bits_ativos(wp & 0x0000000000FF0000ULL) * 4 + DamasCore::contar_bits_ativos(wp & 0x00000000FF000000ULL) * 3 + DamasCore::contar_bits_ativos(wp & 0x000000FF00000000ULL) * 2 + DamasCore::contar_bits_ativos(wp & 0x0000FF0000000000ULL) * 1;
    comp[7] -= DamasCore::contar_bits_ativos(bp & 0x00FF000000000000ULL) * 5 + DamasCore::contar_bits_ativos(bp & 0x0000FF0000000000ULL) * 4 + DamasCore::contar_bits_ativos(bp & 0x000000FF00000000ULL) * 3 + DamasCore::contar_bits_ativos(bp & 0x00000000FF000000ULL) * 2 + DamasCore::contar_bits_ativos(bp & 0x0000000000FF0000ULL) * 1;

    auto pontuar_especialista = [&](int presos) {
        if (presos == 0) return 0;
        if (presos == 1) return 100;
        if (presos == 2) return 400;
        if (presos == 3) return 1500;
        return 3000 + (presos - 4) * 500;
    };
    const DamasCore::BoardBitboard D8_H4 = (1ULL<<3) | (1ULL<<12) | (1ULL<<21) | (1ULL<<30) | (1ULL<<39);
    const DamasCore::BoardBitboard F8_H6 = (1ULL<<5) | (1ULL<<7) | (1ULL<<14) | (1ULL<<23);
    if (wk & D8_H4) comp[8] += pontuar_especialista(__builtin_popcountll(bp & F8_H6));
    const DamasCore::BoardBitboard D8_H4_M = (1ULL<<60) | (1ULL<<51) | (1ULL<<42) | (1ULL<<33) | (1ULL<<24);
    const DamasCore::BoardBitboard F8_H6_M = (1ULL<<58) | (1ULL<<56) | (1ULL<<49) | (1ULL<<40);
    if (bk & D8_H4_M) comp[8] -= pontuar_especialista(__builtin_popcountll(wp & F8_H6_M));
    const DamasCore::BoardBitboard A3_F8 = (1ULL<<40) | (1ULL<<33) | (1ULL<<26) | (1ULL<<19) | (1ULL<<12) | (1ULL<<5);
    const DamasCore::BoardBitboard B8_A5 = (1ULL<<1) | (1ULL<<3) | (1ULL<<8) | (1ULL<<10) | (1ULL<<17) | (1ULL<<24);
    if (wk & A3_F8) comp[8] += pontuar_especialista(__builtin_popcountll(bp & B8_A5));
    const DamasCore::BoardBitboard A3_F8_M = (1ULL<<23) | (1ULL<<30) | (1ULL<<37) | (1ULL<<44) | (1ULL<<51) | (1ULL<<58);
    const DamasCore::BoardBitboard B8_A5_M = (1ULL<<62) | (1ULL<<60) | (1ULL<<55) | (1ULL<<53) | (1ULL<<46) | (1ULL<<39);
    if (bk & A3_F8_M) comp[8] -= pontuar_especialista(__builtin_popcountll(wp & B8_A5_M));

    const DamasCore::BoardBitboard FLANCO_ESQUERDO = 0x0F0F0F0F0F0F0F0FULL;
    const DamasCore::BoardBitboard FLANCO_DIREITO  = 0xF0F0F0F0F0F0F0F0ULL;
    int w_left = DamasCore::contar_bits_ativos(wp & FLANCO_ESQUERDO); int w_right = DamasCore::contar_bits_ativos(wp & FLANCO_DIREITO);
    if (abs(w_left - w_right) > 2) comp[9] -= (abs(w_left - w_right) - 2) * 12;
    int b_left = DamasCore::contar_bits_ativos(bp & FLANCO_ESQUERDO); int b_right = DamasCore::contar_bits_ativos(bp & FLANCO_DIREITO);
    if (abs(b_left - b_right) > 2) comp[9] += (abs(b_left - b_right) - 2) * 12;

    const DamasCore::BoardBitboard WHITE_DEFENDERS = (1ULL << 58) | (1ULL << 60);
    const DamasCore::BoardBitboard BLACK_DEFENDERS = (1ULL << 3) | (1ULL << 5);
    comp[10] += DamasCore::contar_bits_ativos(wp & WHITE_DEFENDERS) * 45;
    comp[10] -= DamasCore::contar_bits_ativos(bp & BLACK_DEFENDERS) * 45;

    const DamasCore::BoardBitboard CENTRO_ABSOLUTO = (1ULL << 26) | (1ULL << 28) | (1ULL << 35) | (1ULL << 37);
    comp[11] += DamasCore::contar_bits_ativos(wp & CENTRO_ABSOLUTO) * 15;
    comp[11] -= DamasCore::contar_bits_ativos(bp & CENTRO_ABSOLUTO) * 15;

    int weak_w = 0, weak_b = 0;
    const DamasCore::BoardBitboard W_WEAK_1 = (1ULL << 46) | (1ULL << 37) | (1ULL << 30);
    const DamasCore::BoardBitboard W_WEAK_2 = (1ULL << 53) | (1ULL << 44) | (1ULL << 37);
    if ((wp & W_WEAK_1) == W_WEAK_1) weak_w -= 40;
    if ((wp & W_WEAK_2) == W_WEAK_2) weak_w -= 40;
    const DamasCore::BoardBitboard B_WEAK_1 = (1ULL << 17) | (1ULL << 26) | (1ULL << 33);
    const DamasCore::BoardBitboard B_WEAK_2 = (1ULL << 10) | (1ULL << 19) | (1ULL << 26);
    if ((bp & B_WEAK_1) == B_WEAK_1) weak_b += 40;
    if ((bp & B_WEAK_2) == B_WEAK_2) weak_b += 40;
    const DamasCore::BoardBitboard W_F4_G3 = (1ULL << 37) | (1ULL << 46);
    if ((wp & W_F4_G3) == W_F4_G3 && (pos.obter_casas_ocupadas() & (1ULL << 39)) != 0 && (((wp | wk) & (1ULL << 62)) == 0)) weak_w -= 60;
    const DamasCore::BoardBitboard B_C5_B6 = (1ULL << 26) | (1ULL << 17);
    if ((bp & B_C5_B6) == B_C5_B6 && (pos.obter_casas_ocupadas() & (1ULL << 24)) != 0 && (((bp | bk) & (1ULL << 1)) == 0)) weak_b += 60;
    const DamasCore::BoardBitboard W_A1_B2_C3 = (1ULL << 56) | (1ULL << 49) | (1ULL << 42);
    const DamasCore::BoardBitboard B_D6_B6_A5 = (1ULL << 19) | (1ULL << 17) | (1ULL << 24);
    if ((wp & W_A1_B2_C3) == W_A1_B2_C3 && (bp & B_D6_B6_A5) == B_D6_B6_A5) weak_w -= 50;
    const DamasCore::BoardBitboard B_H8_G7_F6 = (1ULL << 7) | (1ULL << 14) | (1ULL << 21);
    const DamasCore::BoardBitboard W_E3_G3_H4 = (1ULL << 44) | (1ULL << 46) | (1ULL << 39);
    if ((bp & B_H8_G7_F6) == B_H8_G7_F6 && (wp & W_E3_G3_H4) == W_E3_G3_H4) weak_b += 50;
    const DamasCore::BoardBitboard W_F4_SURROUNDED = (1ULL << 37);
    const DamasCore::BoardBitboard B_BLOCK_F4 = (1ULL << 19) | (1ULL << 21) | (1ULL << 26);
    if ((wp & W_F4_SURROUNDED) != 0 && (bp & B_BLOCK_F4) == B_BLOCK_F4) weak_w -= 45;
    const DamasCore::BoardBitboard B_C5_SURROUNDED = (1ULL << 26);
    const DamasCore::BoardBitboard W_BLOCK_C5 = (1ULL << 44) | (1ULL << 42) | (1ULL << 37);
    if ((bp & B_C5_SURROUNDED) != 0 && (wp & W_BLOCK_C5) == W_BLOCK_C5) weak_b += 45;
    const DamasCore::BoardBitboard W_LEFT_BLOCKED = (1ULL << 56) | (1ULL << 49) | (1ULL << 40);
    const DamasCore::BoardBitboard B_LEFT_WEDGE = (1ULL << 33) | (1ULL << 24);
    if ((wp & W_LEFT_BLOCKED) == W_LEFT_BLOCKED && (bp & B_LEFT_WEDGE) == B_LEFT_WEDGE) weak_w -= 40;
    const DamasCore::BoardBitboard B_RIGHT_BLOCKED = (1ULL << 7) | (1ULL << 14) | (1ULL << 23);
    const DamasCore::BoardBitboard W_RIGHT_WEDGE = (1ULL << 30) | (1ULL << 39);
    if ((bp & B_RIGHT_BLOCKED) == B_RIGHT_BLOCKED && (wp & W_RIGHT_WEDGE) == W_RIGHT_WEDGE) weak_b += 40;
    const DamasCore::BoardBitboard W_RIGHT_LOCKED = (1ULL << 53) | (1ULL << 46) | (1ULL << 55);
    const DamasCore::BoardBitboard B_RIGHT_LOCKERS = (1ULL << 30) | (1ULL << 39);
    if ((wp & W_RIGHT_LOCKED) == W_RIGHT_LOCKED && (bp & B_RIGHT_LOCKERS) == B_RIGHT_LOCKERS) weak_w -= 50;
    const DamasCore::BoardBitboard B_LEFT_LOCKED = (1ULL << 10) | (1ULL << 17) | (1ULL << 8);
    const DamasCore::BoardBitboard W_LEFT_LOCKERS = (1ULL << 33) | (1ULL << 24);
    if ((bp & B_LEFT_LOCKED) == B_LEFT_LOCKED && (wp & W_LEFT_LOCKERS) == W_LEFT_LOCKERS) weak_b += 50;
    const DamasCore::BoardBitboard W_LEFT_VULNERABLE = (1ULL << 26) | (1ULL << 33) | (1ULL << 42) | (1ULL << 40);
    const DamasCore::BoardBitboard B_LEFT_ATTACKERS = (1ULL << 10) | (1ULL << 19) | (1ULL << 8);
    if ((wp & W_LEFT_VULNERABLE) == W_LEFT_VULNERABLE && (bp & B_LEFT_ATTACKERS) == B_LEFT_ATTACKERS) weak_w -= 55;
    const DamasCore::BoardBitboard B_RIGHT_VULNERABLE = (1ULL << 37) | (1ULL << 30) | (1ULL << 21) | (1ULL << 23);
    const DamasCore::BoardBitboard W_RIGHT_ATTACKERS = (1ULL << 53) | (1ULL << 44) | (1ULL << 55);
    if ((bp & B_RIGHT_VULNERABLE) == B_RIGHT_VULNERABLE && (wp & W_RIGHT_ATTACKERS) == W_RIGHT_ATTACKERS) weak_b += 55;
    const DamasCore::BoardBitboard W_BURACO_CATALOGADO_1 = (1ULL << 40) | (1ULL << 49) | (1ULL << 56);
    if ((wp & W_BURACO_CATALOGADO_1) == W_BURACO_CATALOGADO_1) weak_w -= 80;
    const DamasCore::BoardBitboard B_BURACO_CATALOGADO_1 = (1ULL << 7) | (1ULL << 14) | (1ULL << 23);
    if ((bp & B_BURACO_CATALOGADO_1) == B_BURACO_CATALOGADO_1) weak_b += 80;
    comp[12] = weak_w + weak_b;

    const DamasCore::BoardBitboard C5_MASK = (1ULL << 26);
    const DamasCore::BoardBitboard F4_MASK = (1ULL << 37);
    if (((bp | bk) & C5_MASK) != 0 && (((wp | wk) & ((1ULL << 33) | (1ULL << 35))) == 0)) comp[13] -= 35;
    if (((bp | bk) & F4_MASK) != 0 && (((wp | wk) & ((1ULL << 44) | (1ULL << 46))) == 0)) comp[13] -= 35;
    if (((wp | wk) & C5_MASK) != 0 && (((bp | bk) & ((1ULL << 17) | (1ULL << 19))) == 0)) comp[13] += 35;
    if (((wp | wk) & F4_MASK) != 0 && (((bp | bk) & ((1ULL << 28) | (1ULL << 30))) == 0)) comp[13] += 35;

    const DamasCore::BoardBitboard W_UNDEVELOPED_C = (1ULL << 56) | (1ULL << 49) | (1ULL << 42);
    comp[14] -= DamasCore::contar_bits_ativos(wp & W_UNDEVELOPED_C) * 8;
    const DamasCore::BoardBitboard B_UNDEVELOPED_C = (1ULL << 7) | (1ULL << 14) | (1ULL << 21);
    comp[14] += DamasCore::contar_bits_ativos(bp & B_UNDEVELOPED_C) * 8;

    const DamasCore::BoardBitboard W_RANK_5 = (1ULL << 24) | (1ULL << 26) | (1ULL << 28) | (1ULL << 30);
    comp[15] -= DamasCore::contar_bits_ativos(wp & W_RANK_5) * 25;
    const DamasCore::BoardBitboard B_RANK_5 = (1ULL << 33) | (1ULL << 35) | (1ULL << 37) | (1ULL << 39);
    comp[15] += DamasCore::contar_bits_ativos(bp & B_RANK_5) * 25;

    DamasCore::BoardBitboard vazias = ~pos.obter_casas_ocupadas();
    int w_mob = DamasCore::contar_bits_ativos(((wp & DamasCore::NAO_COLUNA_A) >> 9) & vazias) + DamasCore::contar_bits_ativos(((wp & DamasCore::NAO_COLUNA_H) >> 7) & vazias);
    int b_mob = DamasCore::contar_bits_ativos(((bp & DamasCore::NAO_COLUNA_A) << 7) & vazias) + DamasCore::contar_bits_ativos(((bp & DamasCore::NAO_COLUNA_H) << 9) & vazias);
    comp[16] += w_mob * 5;
    comp[16] -= b_mob * 5;

    const DamasCore::BoardBitboard W_SHIELD_1 = (1ULL << 58) | (1ULL << 51) | (1ULL << 60);
    const DamasCore::BoardBitboard W_SHIELD_2 = (1ULL << 60) | (1ULL << 53) | (1ULL << 62);
    if ((wp & W_SHIELD_1) == W_SHIELD_1) comp[17] += 20;
    if ((wp & W_SHIELD_2) == W_SHIELD_2) comp[17] += 20;
    const DamasCore::BoardBitboard B_SHIELD_1 = (1ULL << 3) | (1ULL << 12) | (1ULL << 5);
    const DamasCore::BoardBitboard B_SHIELD_2 = (1ULL << 5) | (1ULL << 14) | (1ULL << 7);
    if ((bp & B_SHIELD_1) == B_SHIELD_1) comp[17] -= 20;
    if ((bp & B_SHIELD_2) == B_SHIELD_2) comp[17] -= 20;

    return comp;
}

// Função auxiliar para espelhar um estado do tabuleiro (troca de cores e orientação)
static DamasCore::BoardState espelhar_estado(const DamasCore::BoardState& st) {
    auto reverter = [](uint64_t x) {
        x = ((x & 0x5555555555555555ULL) << 1) | ((x & 0xAAAAAAAAAAAAAAAAULL) >> 1);
        x = ((x & 0x3333333333333333ULL) << 2) | ((x & 0xCCCCCCCCCCCCCCCCULL) >> 2);
        x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((x & 0xF0F0F0F0F0F0F0F0ULL) >> 4);
        x = ((x & 0x00FF00FF00FF00FFULL) << 8) | ((x & 0xFF00FF00FF00FF00ULL) >> 8);
        x = ((x & 0x0000FFFF0000FFFFULL) << 16) | ((x & 0xFFFF0000FFFF0000ULL) >> 16);
        return (x << 32) | (x >> 32);
    };
    DamasCore::BoardState esp;
    esp.definir_posicao_via_bitboards(
        reverter(st.obter_pretas_peoes()),
        reverter(st.obter_brancas_peoes()),
        reverter(st.obter_pretas_damas()),
        reverter(st.obter_brancas_damas()),
        (st.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? DamasCore::GamePlayer::PRETOS : DamasCore::GamePlayer::BRANCAS
    );
    return esp;
};

// Salva uma sequência de lances vencedora na memória fotográfica para aprendizado instantâneo.
static void salvar_linha_vencedora(const DamasCore::BoardState& root, const std::vector<DamasCore::GameMove>& pv) {
    if (pv.empty()) return;
    
    std::string data_dir = get_data_dir();
    std::string path_padroes_bons = data_dir + "/padroes_bons.bin";
    std::string path_padroes_ruins = data_dir + "/padroes_ruins.bin";

    std::ofstream fout_good(path_padroes_bons, std::ios::binary | std::ios::app);
    std::ofstream fout_bad(path_padroes_ruins, std::ios::binary | std::ios::app);
    if (!fout_good.is_open() || !fout_bad.is_open()) return;

    DamasCore::BoardState current_pos = root;
    bool is_good_for_current_player = true; // A posição raiz é vencedora para o jogador da vez.

    // Itera pela sequência de estados (raiz, após 1º lance, após 2º, etc.)
    for (int i = -1; i < (int)pv.size(); ++i) {
        // Para i >= 0, aplica o lance para chegar no próximo estado
        if (i >= 0) {
            const auto& move = pv[i];
            if (move.mascaras_pecas_capturadas != 0) current_pos.aplicar_movimento_completo_captura(move);
            else current_pos.aplicar_mov_simples(move.casa_origem, move.casa_destino);
            current_pos.alternar_turno();
        }

        uint64_t hash = current_pos.obter_hash();
        uint64_t hash_espelhado = espelhar_estado(current_pos).obter_hash();

        if (is_good_for_current_player) { // Posição vencedora para o jogador da vez
            if (g_padroes_bons.find(hash) == g_padroes_bons.end()) {
                g_padroes_bons.insert(hash);
                fout_good.write(reinterpret_cast<char*>(&hash), sizeof(hash));
            }
            if (g_padroes_bons.find(hash_espelhado) == g_padroes_bons.end()) {
                g_padroes_bons.insert(hash_espelhado);
                fout_good.write(reinterpret_cast<char*>(&hash_espelhado), sizeof(hash_espelhado));
            }
        } else { // Posição perdedora para o jogador da vez
            if (g_padroes_ruins.find(hash) == g_padroes_ruins.end()) {
                g_padroes_ruins.insert(hash);
                fout_bad.write(reinterpret_cast<char*>(&hash), sizeof(hash));
            }
            if (g_padroes_ruins.find(hash_espelhado) == g_padroes_ruins.end()) {
                g_padroes_ruins.insert(hash_espelhado);
                fout_bad.write(reinterpret_cast<char*>(&hash_espelhado), sizeof(hash_espelhado));
            }
        }
        is_good_for_current_player = !is_good_for_current_player;
    }
    fout_good.close();
    fout_bad.close();
}

void aprender_com_correcao(const DamasCore::BoardState& estado_bom, const DamasCore::BoardState& estado_ruim) {
    if (!g_pesos_carregados) carregar_pesos_ia();

    auto comp_bom = extrair_componentes_avaliacao(estado_bom);
    auto comp_ruim = extrair_componentes_avaliacao(estado_ruim);

    // O estado após um movimento reflete que é o turno do adversário.
    // Então quem fez a jogada (quem está sendo ensinado) é o inverso do turno atual do estado.
    DamasCore::GamePlayer jogador_que_moveu = (estado_bom.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? DamasCore::GamePlayer::PRETOS : DamasCore::GamePlayer::BRANCAS;

    for (int i = 0; i < 18; ++i) {
        int score_bom = comp_bom[i];
        int score_ruim = comp_ruim[i];

        // A extração sempre traz pontuação absoluta para as Brancas. Inverte para Pretas se necessário.
        if (jogador_que_moveu == DamasCore::GamePlayer::PRETOS) {
            score_bom = -score_bom;
            score_ruim = -score_ruim;
        }

        int delta = score_bom - score_ruim;

        if (delta > 0) {
            g_pesos_aprendizado[i] += 5; // Acertou o peso: Recompensa aumentada
        } else if (delta < 0) {
            g_pesos_aprendizado[i] -= 10; // Errou o peso favorecendo o erro: Punição forte aumentada
        }

        if (g_pesos_aprendizado[i] < 10) g_pesos_aprendizado[i] = 10;
        if (g_pesos_aprendizado[i] > 400) g_pesos_aprendizado[i] = 400;
    }

    std::string dir = get_data_dir();
    std::string path_pesos = dir + "/pesos_tabela.bin";
    
    std::ofstream fout(path_pesos, std::ios::binary);
    if (fout.is_open()) {
        for (size_t i = 0; i < 18; i++) {
            int p = g_pesos_aprendizado[i];
            fout.write(reinterpret_cast<char*>(&p), sizeof(p));
        }
        fout.close();
    }

    // Novo Parâmetro de Memória Fotográfica: Catalogar Anti-Padrão exato (lance ruim)
    uint64_t hash_ruim = estado_ruim.obter_hash();
    uint64_t hash_ruim_espelhado = espelhar_estado(estado_ruim).obter_hash();
    
    uint64_t hash_bom = estado_bom.obter_hash();
    uint64_t hash_bom_espelhado = espelhar_estado(estado_bom).obter_hash();
    
    // A "Memória Fotográfica" funciona classificando posições como VENCEDORAS ou PERDEDORAS
    // para o jogador que está na vez de jogar naquela posição.

    // 1. Classificar o resultado do LANCE RUIM da IA.
    // A posição `estado_ruim` é o resultado do lance errado. Nela, é a vez do oponente (humano) jogar.
    // Como a IA errou, esta é uma posição VENCEDORA para o humano.
    // Portanto, salvamos seu hash em `g_padroes_bons`.
    {
        std::string path_padroes_bons = dir + "/padroes_bons.bin";
        std::ofstream fout_good(path_padroes_bons, std::ios::binary | std::ios::app);
        if (fout_good.is_open()) {
            if (g_padroes_bons.find(hash_ruim) == g_padroes_bons.end()) {
                g_padroes_bons.insert(hash_ruim);
                fout_good.write(reinterpret_cast<char*>(&hash_ruim), sizeof(hash_ruim));
            }
            if (g_padroes_bons.find(hash_ruim_espelhado) == g_padroes_bons.end()) {
                g_padroes_bons.insert(hash_ruim_espelhado);
                fout_good.write(reinterpret_cast<char*>(&hash_ruim_espelhado), sizeof(hash_ruim_espelhado));
            }
            fout_good.close();
        }
    }

    // 2. Classificar o resultado do LANCE BOM (corrigido pelo usuário).
    // A posição `estado_bom` é o resultado do lance correto que a IA deveria ter feito.
    // Nela, também é a vez do oponente (humano) jogar.
    // Como a IA fez um lance bom, esta é uma posição PERDEDORA para o humano.
    // Portanto, salvamos seu hash em `g_padroes_ruins`.
    {
        std::string path_padroes_ruins = dir + "/padroes_ruins.bin";
        std::ofstream fout_bad(path_padroes_ruins, std::ios::binary | std::ios::app);
        if (fout_bad.is_open()) {
            if (g_padroes_ruins.find(hash_bom) == g_padroes_ruins.end()) {
                g_padroes_ruins.insert(hash_bom);
                fout_bad.write(reinterpret_cast<char*>(&hash_bom), sizeof(hash_bom));
            }
            if (g_padroes_ruins.find(hash_bom_espelhado) == g_padroes_ruins.end()) {
                g_padroes_ruins.insert(hash_bom_espelhado);
                fout_bad.write(reinterpret_cast<char*>(&hash_bom_espelhado), sizeof(hash_bom_espelhado));
            }
            fout_bad.close();
        }
    }
}

int DamasEngine::evaluate(const DamasCore::BoardState& pos) {
    if (!g_pesos_carregados) carregar_pesos_ia();

    const DamasCore::BoardBitboard wp = pos.obter_brancas_peoes();
    const DamasCore::BoardBitboard bp = pos.obter_pretas_peoes();
    const DamasCore::BoardBitboard wk = pos.obter_brancas_damas();
    const DamasCore::BoardBitboard bk = pos.obter_pretas_damas();

    int score = 0;
    
    // Pega os 18 pesos de aprendizado (Base 100 = 100%). Evita divisão por zero.
    auto peso = [](int indice) { 
        return g_pesos_aprendizado[indice]; 
    };

    // 1. Material Básico (Dama posicionalmente mais forte)
    score += (DamasCore::contar_bits_ativos(wp) - DamasCore::contar_bits_ativos(bp)) * (100 * peso(0) / 100);
    score += (DamasCore::contar_bits_ativos(wk) - DamasCore::contar_bits_ativos(bk)) * (400 * peso(0) / 100);
    
    // 2. Defesa da Fileira de Coroação (Back Rank) - Fundamental para não levar golpes
    score += DamasCore::contar_bits_ativos(wp & DamasCore::RANK_PRIMEIRA_PRETA) * (15 * peso(1) / 100);
    score -= DamasCore::contar_bits_ativos(bp & DamasCore::RANK_ULTIMA_BRANCA) * (15 * peso(1) / 100);

    // 3. Peões Apoiados (Conectados e defendidos pela diagonal traseira)
    DamasCore::BoardBitboard supported_wp = wp & (((wp & DamasCore::NAO_COLUNA_A) >> 9) | ((wp & DamasCore::NAO_COLUNA_H) >> 7));
    DamasCore::BoardBitboard supported_bp = bp & (((bp & DamasCore::NAO_COLUNA_A) << 7) | ((bp & DamasCore::NAO_COLUNA_H) << 9));
    score += DamasCore::contar_bits_ativos(supported_wp) * (12 * peso(2) / 100);
    score -= DamasCore::contar_bits_ativos(supported_bp) * (12 * peso(2) / 100);

    // 4. Pedra Cão (Outposts Fortes) - Peças asfixiantes e indestrutíveis
    // Uma peça Branca em c5 (26) ou f6 (21) apoiada
    const DamasCore::BoardBitboard WHITE_OUTPOSTS = (1ULL << 21) | (1ULL << 26);
    score += DamasCore::contar_bits_ativos(supported_wp & WHITE_OUTPOSTS) * (30 * peso(3) / 100);

    // Uma peça Preta em c3 (42) ou f4 (37) apoiada
    const DamasCore::BoardBitboard BLACK_OUTPOSTS = (1ULL << 37) | (1ULL << 42);
    score -= DamasCore::contar_bits_ativos(supported_bp & BLACK_OUTPOSTS) * (30 * peso(3) / 100);

    // 5. Controle do Centro Expansivo
    const DamasCore::BoardBitboard CENTER = 0x00003C3C3C3C0000ULL;
    score += DamasCore::contar_bits_ativos(wp & CENTER) * (10 * peso(4) / 100);
    score -= DamasCore::contar_bits_ativos(bp & CENTER) * (10 * peso(4) / 100);
    score += DamasCore::contar_bits_ativos(wk & CENTER) * (25 * peso(4) / 100);
    score -= DamasCore::contar_bits_ativos(bk & CENTER) * (25 * peso(4) / 100);

    // 6. Controle do Carreirão (Diagonal Principal a1-h8)
    const DamasCore::BoardBitboard CARREIRAO = (1ULL<<56) | (1ULL<<49) | (1ULL<<42) | (1ULL<<35) | (1ULL<<28) | (1ULL<<21) | (1ULL<<14) | (1ULL<<7);
    score += DamasCore::contar_bits_ativos(wp & CARREIRAO) * (4 * peso(5) / 100);
    score -= DamasCore::contar_bits_ativos(bp & CARREIRAO) * (4 * peso(5) / 100);
    score += DamasCore::contar_bits_ativos(wk & CARREIRAO) * (25 * peso(5) / 100);
    score -= DamasCore::contar_bits_ativos(bk & CARREIRAO) * (25 * peso(5) / 100);

    // 7. Peões nas Bordas (Seguros, porém inflexíveis e sem domínio de jogo)
    const DamasCore::BoardBitboard BORDAS = DamasCore::BITBOARD_COLUNA_A | DamasCore::BITBOARD_COLUNA_H;
    score += DamasCore::contar_bits_ativos(wp & BORDAS) * (3 * peso(6) / 100);
    score -= DamasCore::contar_bits_ativos(bp & BORDAS) * (3 * peso(6) / 100);

    // 8. Avanço de Peões (Tie-breaker pequeno para incentivar o jogo, evita kamikazes)
    score += DamasCore::contar_bits_ativos(wp & 0x000000000000FF00ULL) * (5 * peso(7) / 100); // Linha 1
    score += DamasCore::contar_bits_ativos(wp & 0x0000000000FF0000ULL) * (4 * peso(7) / 100); // Linha 2
    score += DamasCore::contar_bits_ativos(wp & 0x00000000FF000000ULL) * (3 * peso(7) / 100);  // Linha 3
    score += DamasCore::contar_bits_ativos(wp & 0x000000FF00000000ULL) * (2 * peso(7) / 100);  // Linha 4
    score += DamasCore::contar_bits_ativos(wp & 0x0000FF0000000000ULL) * (1 * peso(7) / 100);  // Linha 5

    score -= DamasCore::contar_bits_ativos(bp & 0x00FF000000000000ULL) * (5 * peso(7) / 100); // Linha 6
    score -= DamasCore::contar_bits_ativos(bp & 0x0000FF0000000000ULL) * (4 * peso(7) / 100); // Linha 5
    score -= DamasCore::contar_bits_ativos(bp & 0x000000FF00000000ULL) * (3 * peso(7) / 100);  // Linha 4
    score -= DamasCore::contar_bits_ativos(bp & 0x00000000FF000000ULL) * (2 * peso(7) / 100);  // Linha 3
    score -= DamasCore::contar_bits_ativos(bp & 0x0000000000FF0000ULL) * (1 * peso(7) / 100);  // Linha 2

    // 9. Especialistas (Armadilhas e posições fortes/fracas nativas)
    auto pontuar_especialista = [&](int presos) {
        if (presos == 0) return 0;
        int base_val = 0;
        if (presos == 1) base_val = 100;
        else if (presos == 2) base_val = 400;
        else if (presos == 3) base_val = 1500;
        else base_val = 3000 + (presos - 4) * 500;
        return base_val * peso(8) / 100;
    };

    const DamasCore::BoardBitboard D8_H4 = (1ULL<<3) | (1ULL<<12) | (1ULL<<21) | (1ULL<<30) | (1ULL<<39);
    const DamasCore::BoardBitboard F8_H6 = (1ULL<<5) | (1ULL<<7) | (1ULL<<14) | (1ULL<<23);
    if (wk & D8_H4) score += pontuar_especialista(__builtin_popcountll(bp & F8_H6));
    const DamasCore::BoardBitboard D8_H4_M = (1ULL<<60) | (1ULL<<51) | (1ULL<<42) | (1ULL<<33) | (1ULL<<24);
    const DamasCore::BoardBitboard F8_H6_M = (1ULL<<58) | (1ULL<<56) | (1ULL<<49) | (1ULL<<40);
    if (bk & D8_H4_M) score -= pontuar_especialista(__builtin_popcountll(wp & F8_H6_M));

    const DamasCore::BoardBitboard A3_F8 = (1ULL<<40) | (1ULL<<33) | (1ULL<<26) | (1ULL<<19) | (1ULL<<12) | (1ULL<<5);
    const DamasCore::BoardBitboard B8_A5 = (1ULL<<1) | (1ULL<<3) | (1ULL<<8) | (1ULL<<10) | (1ULL<<17) | (1ULL<<24);
    if (wk & A3_F8) score += pontuar_especialista(__builtin_popcountll(bp & B8_A5));
    const DamasCore::BoardBitboard A3_F8_M = (1ULL<<23) | (1ULL<<30) | (1ULL<<37) | (1ULL<<44) | (1ULL<<51) | (1ULL<<58);
    const DamasCore::BoardBitboard B8_A5_M = (1ULL<<62) | (1ULL<<60) | (1ULL<<55) | (1ULL<<53) | (1ULL<<46) | (1ULL<<39);
    if (bk & A3_F8_M) score -= pontuar_especialista(__builtin_popcountll(wp & B8_A5_M));

        // 10. Balanceamento do Tabuleiro (Evitar posições "pênsas")
    const DamasCore::BoardBitboard FLANCO_ESQUERDO = 0x0F0F0F0F0F0F0F0FULL; // Colunas a, b, c, d
    const DamasCore::BoardBitboard FLANCO_DIREITO  = 0xF0F0F0F0F0F0F0F0ULL; // Colunas e, f, g, h
    
    int w_left = DamasCore::contar_bits_ativos(wp & FLANCO_ESQUERDO);
    int w_right = DamasCore::contar_bits_ativos(wp & FLANCO_DIREITO);
    if (abs(w_left - w_right) > 2) score -= (abs(w_left - w_right) - 2) * 12 * peso(9) / 100; // Penalidade

    int b_left = DamasCore::contar_bits_ativos(bp & FLANCO_ESQUERDO);
    int b_right = DamasCore::contar_bits_ativos(bp & FLANCO_DIREITO);
    if (abs(b_left - b_right) > 2) score += (abs(b_left - b_right) - 2) * 12 * peso(9) / 100; // Penalidade

    // 11. Estrutura de Defesa Forte do Fundo (Base sólida)
    const DamasCore::BoardBitboard WHITE_DEFENDERS = (1ULL << 58) | (1ULL << 60); // c1, e1
    const DamasCore::BoardBitboard BLACK_DEFENDERS = (1ULL << 3) | (1ULL << 5);   // d8, f8
    score += DamasCore::contar_bits_ativos(wp & WHITE_DEFENDERS) * 45 * peso(10) / 100; // Penalidade severa ao expor a base
    score -= DamasCore::contar_bits_ativos(bp & BLACK_DEFENDERS) * 45 * peso(10) / 100;

    // 12. Domínio do Centro Absoluto e Pressão
    const DamasCore::BoardBitboard CENTRO_ABSOLUTO = (1ULL << 26) | (1ULL << 28) | (1ULL << 35) | (1ULL << 37);
    score += DamasCore::contar_bits_ativos(wp & CENTRO_ABSOLUTO) * 15 * peso(11) / 100;
    score -= DamasCore::contar_bits_ativos(bp & CENTRO_ABSOLUTO) * 15 * peso(11) / 100;

    // 13. Estruturas Fracas Específicas (Flancos vulneráveis e desconectados)
    const DamasCore::BoardBitboard W_WEAK_1 = (1ULL << 46) | (1ULL << 37) | (1ULL << 30); // g3(46), f4(37), g5(30)
    const DamasCore::BoardBitboard W_WEAK_2 = (1ULL << 53) | (1ULL << 44) | (1ULL << 37); // f2(53), e3(44), f4(37)
    if ((wp & W_WEAK_1) == W_WEAK_1) score -= 40 * peso(12) / 100;
    if ((wp & W_WEAK_2) == W_WEAK_2) score -= 40 * peso(12) / 100;

    const DamasCore::BoardBitboard B_WEAK_1 = (1ULL << 17) | (1ULL << 26) | (1ULL << 33); // b6(17), c5(26), b4(33)
    const DamasCore::BoardBitboard B_WEAK_2 = (1ULL << 10) | (1ULL << 19) | (1ULL << 26); // c7(10), d6(19), c5(26)
    if ((bp & B_WEAK_1) == B_WEAK_1) score += 40 * peso(12) / 100;
    if ((bp & B_WEAK_2) == B_WEAK_2) score += 40 * peso(12) / 100;

    // 13.b Estrutura f4, g3, h4 vulnerável sem g1
    const DamasCore::BoardBitboard W_F4_G3 = (1ULL << 37) | (1ULL << 46);
    if ((wp & W_F4_G3) == W_F4_G3) {
        if ((pos.obter_casas_ocupadas() & (1ULL << 39)) != 0) { // h4 ocupada
            if (((wp | wk) & (1ULL << 62)) == 0) { // g1 vazia
                score -= 60 * peso(12) / 100; // Penalidade forte
            }
        }
    }

    // Espelhado para as Pretas: c5, b6, a5 sem a base b8
    const DamasCore::BoardBitboard B_C5_B6 = (1ULL << 26) | (1ULL << 17);
    if ((bp & B_C5_B6) == B_C5_B6) {
        if ((pos.obter_casas_ocupadas() & (1ULL << 24)) != 0) { // a5 ocupada
            if (((bp | bk) & (1ULL << 1)) == 0) { // b8 vazia
                score += (60 * peso(12) / 100);
            }
        }
    }

    // 13.c Estrutura a1, b2, c3 bloqueada e vulnerável contra d6, b6, a5
    const DamasCore::BoardBitboard W_A1_B2_C3 = (1ULL << 56) | (1ULL << 49) | (1ULL << 42);
    const DamasCore::BoardBitboard B_D6_B6_A5 = (1ULL << 19) | (1ULL << 17) | (1ULL << 24);
    if ((wp & W_A1_B2_C3) == W_A1_B2_C3 && (bp & B_D6_B6_A5) == B_D6_B6_A5) {
        score -= 50 * peso(12) / 100; // Penalidade severa por bloqueio
    }

    // Espelhado para as Pretas: h8, g7, f6 vulnerável contra e3, g3, h4
    const DamasCore::BoardBitboard B_H8_G7_F6 = (1ULL << 7) | (1ULL << 14) | (1ULL << 21);
    const DamasCore::BoardBitboard W_E3_G3_H4 = (1ULL << 44) | (1ULL << 46) | (1ULL << 39);
    if ((bp & B_H8_G7_F6) == B_H8_G7_F6 && (wp & W_E3_G3_H4) == W_E3_G3_H4) {
        score += 50 * peso(12) / 100; // Penalidade para as Pretas
    }

    // 13.d Peça avançada e cercada (f4 para Brancas, c5 para Pretas)
    const DamasCore::BoardBitboard W_F4_SURROUNDED = (1ULL << 37);
    const DamasCore::BoardBitboard B_BLOCK_F4 = (1ULL << 19) | (1ULL << 21) | (1ULL << 26); // d6, f6, c5
    if ((wp & W_F4_SURROUNDED) != 0 && (bp & B_BLOCK_F4) == B_BLOCK_F4) {
        score -= 45 * peso(12) / 100; // Penalidade por ficar cercado
    }

    // Espelhado para as Pretas: c5 cercada por Brancas em e3, c3 e f4
    const DamasCore::BoardBitboard B_C5_SURROUNDED = (1ULL << 26);
    const DamasCore::BoardBitboard W_BLOCK_C5 = (1ULL << 44) | (1ULL << 42) | (1ULL << 37); // e3, c3, f4
    if ((bp & B_C5_SURROUNDED) != 0 && (wp & W_BLOCK_C5) == W_BLOCK_C5) {
        score += 45 * peso(12) / 100; // Penalidade para as Pretas
    }

    // 13.e Flanco preso (a1, b2, a3 bloqueados por b4, a5)
    const DamasCore::BoardBitboard W_LEFT_BLOCKED = (1ULL << 56) | (1ULL << 49) | (1ULL << 40); // a1, b2, a3
    const DamasCore::BoardBitboard B_LEFT_WEDGE = (1ULL << 33) | (1ULL << 24); // b4, a5
    if ((wp & W_LEFT_BLOCKED) == W_LEFT_BLOCKED && (bp & B_LEFT_WEDGE) == B_LEFT_WEDGE) {
        score -= 40 * peso(12) / 100; // Penalidade forte por flanco preso
    }

    // Espelhado para as Pretas: h8, g7, h6 bloqueados por g5, h4
    const DamasCore::BoardBitboard B_RIGHT_BLOCKED = (1ULL << 7) | (1ULL << 14) | (1ULL << 23); // h8, g7, h6
    const DamasCore::BoardBitboard W_RIGHT_WEDGE = (1ULL << 30) | (1ULL << 39); // g5, h4
    if ((bp & B_RIGHT_BLOCKED) == B_RIGHT_BLOCKED && (wp & W_RIGHT_WEDGE) == W_RIGHT_WEDGE) {
        score += 40 * peso(12) / 100; // Penalidade forte para as Pretas
    }

    // 13.f Ala Direita Engessada (f2, g3, h2 travados por g5, h4)
    const DamasCore::BoardBitboard W_RIGHT_LOCKED = (1ULL << 53) | (1ULL << 46) | (1ULL << 55); // f2(53), g3(46), h2(55)
    const DamasCore::BoardBitboard B_RIGHT_LOCKERS = (1ULL << 30) | (1ULL << 39); // g5(30), h4(39)
    if ((wp & W_RIGHT_LOCKED) == W_RIGHT_LOCKED && (bp & B_RIGHT_LOCKERS) == B_RIGHT_LOCKERS) {
        score -= 50 * peso(12) / 100; // Penalidade forte por paralisia na ala direita
    }

    // Espelhado para as Pretas: Ala Esquerda Engessada (c7, b6, a7 travados por b4, a5)
    const DamasCore::BoardBitboard B_LEFT_LOCKED = (1ULL << 10) | (1ULL << 17) | (1ULL << 8); // c7(10), b6(17), a7(8)
    const DamasCore::BoardBitboard W_LEFT_LOCKERS = (1ULL << 33) | (1ULL << 24); // b4(33), a5(24)
    if ((bp & B_LEFT_LOCKED) == B_LEFT_LOCKED && (wp & W_LEFT_LOCKERS) == W_LEFT_LOCKERS) {
        score += 50 * peso(12) / 100; // Penalidade forte para as Pretas
    }

    // 13.g Ala Esquerda Vulnerável à Ruptura (c5, b4, c3, a3 contra c7, d6, a7)
    const DamasCore::BoardBitboard W_LEFT_VULNERABLE = (1ULL << 26) | (1ULL << 33) | (1ULL << 42) | (1ULL << 40); // c5(26), b4(33), c3(42), a3(40)
    const DamasCore::BoardBitboard B_LEFT_ATTACKERS = (1ULL << 10) | (1ULL << 19) | (1ULL << 8); // c7(10), d6(19), a7(8)
    if ((wp & W_LEFT_VULNERABLE) == W_LEFT_VULNERABLE && (bp & B_LEFT_ATTACKERS) == B_LEFT_ATTACKERS) {
        score -= 55 * peso(12) / 100; // Penalidade severa por vulnerabilidade à ruptura c7-b6
    }

    // Espelhado para as Pretas: Ala Direita Vulnerável à Ruptura (f4, g5, f6, h6 contra f2, e3, h2)
    const DamasCore::BoardBitboard B_RIGHT_VULNERABLE = (1ULL << 37) | (1ULL << 30) | (1ULL << 21) | (1ULL << 23); // f4(37), g5(30), f6(21), h6(23)
    const DamasCore::BoardBitboard W_RIGHT_ATTACKERS = (1ULL << 53) | (1ULL << 44) | (1ULL << 55); // f2(53), e3(44), h2(55)
    if ((bp & B_RIGHT_VULNERABLE) == B_RIGHT_VULNERABLE && (wp & W_RIGHT_ATTACKERS) == W_RIGHT_ATTACKERS) {
        score += 55 * peso(12) / 100; // Penalidade severa para as Pretas
    }

    // -------------------------------------------------------------------------
    // 13.h POSIÇÕES FRACAS CATALOGADAS (Ensino Direto pelo Desenvolvedor)
    // Use este bloco para inserir os padrões ruins que o seu script busca.sh 
    // identificou. A IA perderá muitos pontos se entrar nessas formações.
    // 
    // Exemplo 1: Brancas aglomeradas em a3, b2, a1 sem saída
    const DamasCore::BoardBitboard W_BURACO_CATALOGADO_1 = (1ULL << 40) | (1ULL << 49) | (1ULL << 56);
    if ((wp & W_BURACO_CATALOGADO_1) == W_BURACO_CATALOGADO_1) {
        score -= 80 * peso(12) / 100; // Punição gravíssima forçará a IA a abortar a linha
    }
    
    // Exemplo 2: Pretas formando um bloqueio inútil no flanco direito
    const DamasCore::BoardBitboard B_BURACO_CATALOGADO_1 = (1ULL << 7) | (1ULL << 14) | (1ULL << 23); // h8, g7, h6
    if ((bp & B_BURACO_CATALOGADO_1) == B_BURACO_CATALOGADO_1) {
        score += 80 * peso(12) / 100; // Soma pontos nas pretas (Punição para as Pretas)
    }
    // -------------------------------------------------------------------------

    // 14. Combate aos Postos Avançados Inimigos (Pressão sobre c5 e f4)
    const DamasCore::BoardBitboard C5_MASK = (1ULL << 26);
    const DamasCore::BoardBitboard F4_MASK = (1ULL << 37);

    if (((bp | bk) & C5_MASK) != 0) {
        if (((wp | wk) & ((1ULL << 33) | (1ULL << 35))) == 0) score -= 35 * peso(13) / 100;
    }
    if (((bp | bk) & F4_MASK) != 0) {
        if (((wp | wk) & ((1ULL << 44) | (1ULL << 46))) == 0) score -= 35 * peso(13) / 100;
    }
    if (((wp | wk) & C5_MASK) != 0) {
        if (((bp | bk) & ((1ULL << 17) | (1ULL << 19))) == 0) score += 35 * peso(13) / 100;
    }
    if (((wp | wk) & F4_MASK) != 0) {
        if (((bp | bk) & ((1ULL << 28) | (1ULL << 30))) == 0) score += 35 * peso(13) / 100;
    }

    // 15. Desenvolvimento do Carreirão Inferior (Diagonal Principal)
    const DamasCore::BoardBitboard W_UNDEVELOPED_C = (1ULL << 56) | (1ULL << 49) | (1ULL << 42); // a1, b2, c3
    score -= DamasCore::contar_bits_ativos(wp & W_UNDEVELOPED_C) * 8 * peso(14) / 100;
    const DamasCore::BoardBitboard B_UNDEVELOPED_C = (1ULL << 7) | (1ULL << 14) | (1ULL << 21); // h8, g7, f6
    score += DamasCore::contar_bits_ativos(bp & B_UNDEVELOPED_C) * 8 * peso(14) / 100;

    // 16. Cuidado no Avanço para a 5ª Fileira
    const DamasCore::BoardBitboard W_RANK_5 = (1ULL << 24) | (1ULL << 26) | (1ULL << 28) | (1ULL << 30); // a5, c5, e5, g5
    score -= DamasCore::contar_bits_ativos(wp & W_RANK_5) * 25 * peso(15) / 100; // Penalidade aumentada para forçar lances mais seguros
    const DamasCore::BoardBitboard B_RANK_5 = (1ULL << 33) | (1ULL << 35) | (1ULL << 37) | (1ULL << 39); // b4, d4, f4, h4
    score += DamasCore::contar_bits_ativos(bp & B_RANK_5) * 25 * peso(15) / 100; // Penalidade aumentada para forçar lances mais seguros

    // 17. Mobilidade (Liberdade de Movimento)
    DamasCore::BoardBitboard vazias = ~pos.obter_casas_ocupadas();
    int w_mob = DamasCore::contar_bits_ativos(((wp & DamasCore::NAO_COLUNA_A) >> 9) & vazias) + 
                DamasCore::contar_bits_ativos(((wp & DamasCore::NAO_COLUNA_H) >> 7) & vazias);
    int b_mob = DamasCore::contar_bits_ativos(((bp & DamasCore::NAO_COLUNA_A) << 7) & vazias) + 
                DamasCore::contar_bits_ativos(((bp & DamasCore::NAO_COLUNA_H) << 9) & vazias);
    score += w_mob * 5 * peso(16) / 100;
    score -= b_mob * 5 * peso(16) / 100;

    // 18. Escudos da Base (Triângulos de Proteção)
    const DamasCore::BoardBitboard W_SHIELD_1 = (1ULL << 58) | (1ULL << 51) | (1ULL << 60); // c1(58), d2(51), e1(60)
    const DamasCore::BoardBitboard W_SHIELD_2 = (1ULL << 60) | (1ULL << 53) | (1ULL << 62); // e1(60), f2(53), g1(62)
    if ((wp & W_SHIELD_1) == W_SHIELD_1) score += 20 * peso(17) / 100;
    if ((wp & W_SHIELD_2) == W_SHIELD_2) score += 20 * peso(17) / 100;

    const DamasCore::BoardBitboard B_SHIELD_1 = (1ULL << 3) | (1ULL << 12) | (1ULL << 5); // d8(3), e7(12), f8(5)
    const DamasCore::BoardBitboard B_SHIELD_2 = (1ULL << 5) | (1ULL << 14) | (1ULL << 7); // f8(5), g7(14), h8(7)
    if ((bp & B_SHIELD_1) == B_SHIELD_1) score -= 20 * peso(17) / 100;
    if ((bp & B_SHIELD_2) == B_SHIELD_2) score -= 20 * peso(17) / 100;

    // 19. Aplica a experiência de aprendizado (Reinforcement Learning Simplificado)

    // Pega o primeiro peso como um bônus/penalidade global de confiança do motor
    // (Como a base inicial é 100, subtraímos 100 para achar a diferença exata de vitórias e derrotas da IA)
    int bonus_experiencia = (g_pesos_aprendizado[0] - 100); 
    if (bonus_experiencia != 0) score += bonus_experiencia;

    // 19. Memória Fotográfica de Erros (Novo Parâmetro Dinâmico)
    // Punição de pontuação monstruosa caso entre em um cenário exato fotografado como "ruim"
    // Esta verificação tem prioridade sobre a avaliação normal.
    uint64_t current_hash = pos.obter_hash();
    if (g_padroes_ruins.count(current_hash)) {
        // Posição perdedora para o jogador atual. Retorna score de mate contra.
        return -SCORE_MATE;
    } else if (g_padroes_bons.count(current_hash)) {
        // Posição vencedora para o jogador atual. Retorna score de mate a favor.
        return SCORE_MATE;
    }
    
    return (pos.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? score : -score;
}

void DamasEngine::sort_moves(DamasCore::MoveList& list, uint16_t tt_move_compact, int ply, unsigned int thread_id) {
    int scores[128];
    for(size_t i=0; i<list.size(); ++i) {
        const auto& m = list[i];
        uint16_t m_comp = (m.casa_origem & 0x3F) | ((m.casa_destino & 0x3F) << 6);
        if (m_comp == tt_move_compact) scores[i] = 30000000;
        else if (m.mascaras_pecas_capturadas != 0) scores[i] = 20000000 + DamasCore::contar_bits_ativos(m.mascaras_pecas_capturadas) * 1000;
        else {
            if (m == _thread_data[thread_id].killers[ply][0]) scores[i] = 2000000;
            else if (m == _thread_data[thread_id].killers[ply][1]) scores[i] = 1000000;
            else scores[i] = _thread_data[thread_id].history[m.casa_origem][m.casa_destino];
        }
    }
    for(size_t i=1; i<list.size(); ++i) {
        DamasCore::GameMove key = list[i]; int key_s = scores[i]; int j = i - 1;
        while(j >= 0 && scores[j] < key_s) { list[j+1] = list[j]; scores[j+1] = scores[j]; j--; }
        list[j+1] = key; scores[j+1] = key_s;
    }
}

void DamasEngine::extract_pv(DamasCore::BoardState pos, std::vector<DamasCore::GameMove>& pv) {
    pv.clear();
    for (int i = 0; i < 20; ++i) {
        uint64_t hash = pos.obter_hash();
        TTEntry& tte = _tt[hash & (TT_SIZE - 1)];
        if (tte.hash != hash || tte.move == 0xFFFF) break;
        
        int orig = tte.move & 0x3F;
        int dest = (tte.move >> 6) & 0x3F;

        DamasCore::MoveList moves = DamasCore::gerar_todas_capturas_maximais(pos);
        if (moves.empty()) {
            DamasCore::BoardBitboard pieces = (pos.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? pos.obter_todas_pecas_brancas() : pos.obter_todas_pecas_pretas();
            while(pieces) {
                int sq = __builtin_ctzll(pieces);
                bool is_pawn = ((pos.obter_brancas_peoes() | pos.obter_pretas_peoes()) & (1ULL << sq));
                DamasCore::BoardBitboard dests = is_pawn ? DamasCore::gerar_movimentos_simples_peao(pos, sq) : DamasCore::gerar_movimentos_simples_dama(pos, sq);
                while(dests) { moves.push_back({sq, __builtin_ctzll(dests), 0}); dests &= dests - 1; }
                pieces &= pieces - 1;
            }
        }
        
        DamasCore::GameMove full_move; 
        for(size_t j=0; j<moves.size(); ++j) {
            if (moves[j].casa_origem == orig && moves[j].casa_destino == dest) { full_move = moves[j]; break; }
        }
        if (full_move.casa_origem == -1) break; // Lance invalido na TT

        pv.push_back(full_move);
        if (full_move.mascaras_pecas_capturadas != 0) pos.aplicar_movimento_completo_captura(full_move);
        else pos.aplicar_mov_simples(full_move.casa_origem, full_move.casa_destino);
        pos.alternar_turno();
    }
}

int DamasEngine::quiescence(DamasCore::BoardState pos, int alpha, int beta, int q_ply, unsigned int thread_id) {
    check_time();
    if (_stop) return 0;

    // Bloqueio Imediato: Anti-Padrão Fotográfico durante as trocas
    uint64_t hash = pos.obter_hash();
    if (g_padroes_ruins.count(hash)) return -SCORE_MATE + q_ply;
    if (g_padroes_bons.count(hash)) return SCORE_MATE - q_ply;

    DamasCore::MoveList caps = DamasCore::gerar_todas_capturas_maximais(pos);
    if (caps.empty()) {
        int score = evaluate(pos);
        if (q_ply >= 16 && score + 150 < alpha) return score; // Poda absoluta caso exploda
        if (score >= beta) return score;
        if (score > alpha) alpha = score;
        return score;
    }

    int best = -SCORE_INF;
    for (size_t i = 0; i < caps.size(); ++i) {
        DamasCore::BoardState next = pos;
        next.aplicar_movimento_completo_captura(caps[i]);
        next.alternar_turno();
        int val = -quiescence(next, -beta, -alpha, q_ply + 1, thread_id);
        if (_stop) return 0;
        if (val > best) best = val;
        if (alpha < val) alpha = val;
        if (alpha >= beta) break;
    }
    return best;
}

int DamasEngine::alpha_beta(DamasCore::BoardState pos, int depth, int alpha, int beta, int ply, bool do_null, unsigned int thread_id) {
    check_time();
    if (_stop) return 0;
    
    if (g_debug_arvore) {
        std::string indent(ply * 2, ' ');
        std::cout << indent << "[AlphaBeta] -> Entrada ply:" << ply << " depth:" << depth << " alpha:" << alpha << " beta:" << beta << "\n";
    }

    if (ply >= 1023) return evaluate(pos); // Previne estouro de limite do array de historico de repeticao

    if (pos.obter_relogio_meio_movimento() >= 40) {
        if (g_debug_arvore) {
            std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Poda Empate 20 Lances\n";
        }
        return 0; // Poda absoluta para Empate (Regra dos 20 lances)
    }

    uint64_t hash = pos.obter_hash();
    if (ply > 0) {
        for (int i = ply - 2; i >= 0; i -= 2) if (_thread_data[thread_id].rep_stack[i] == hash) return 0;
        
        // Poda por Memória Fotográfica: Se a posição é conhecida, retorna o resultado imediatamente.
        if (g_padroes_ruins.count(hash)) 
            return -SCORE_MATE + ply; // Posição perdedora, retorna score muito baixo.
        if (g_padroes_bons.count(hash))
            return SCORE_MATE - ply; // Posição vencedora, retorna score muito alto.
    }
    _thread_data[thread_id].rep_stack[ply] = hash;

    int orig_alpha = alpha;
    TTEntry& tte = _tt[hash & (TT_SIZE - 1)];
    uint16_t tt_move_compact = 0xFFFF;
    
    if (tte.hash == hash) {
        // A verificação de profundidade e o uso da pontuação da TT são inerentemente
        // seguros para threads, pois estamos apenas lendo. Uma escrita simultânea
        // por outra thread pode levar a uma falha na verificação do hash ou ao uso de
        // dados ligeiramente desatualizados, o que é aceitável em troca de desempenho.
        tt_move_compact = tte.move;
        if (tte.depth >= depth && ply > 0) {
            int tt_score = tte.score;
            if (tt_score > 19000) tt_score -= ply;
            if (tt_score < -19000) tt_score += ply;
            if (tte.flag == TT_EXACT) return tt_score;
            if (tte.flag == TT_ALPHA) beta = std::min(beta, tt_score);
            if (tte.flag == TT_BETA) alpha = std::max(alpha, tt_score);
            if (alpha >= beta) {
            if (g_debug_arvore) {
                std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Poda TT score:" << tt_score << "\n";
            }
                return tt_score;
            }
        }
    }

    // EGTB Probing (Bases de Finais)
    int total_pieces = DamasCore::contar_bits_ativos(pos.obter_casas_ocupadas());
        if (ply > 0 && total_pieces <= 6) { 
        int dtm = 0;
        uint16_t tb_val = EGTB::probe(pos, dtm);
        if (tb_val != EGTB::VAL_UNKNOWN) {
            int tb_score = 0;
            if (tb_val == EGTB::VAL_WIN) tb_score = SCORE_MATE - (ply + dtm);
            else if (tb_val == EGTB::VAL_LOSS) tb_score = -SCORE_MATE + (ply + dtm);
            else if (tb_val == EGTB::VAL_DRAW) tb_score = 0;
            
            int tt_store = tb_score;
            if (tt_store > 19000) tt_store += ply;
            if (tt_store < -19000) tt_store -= ply;
            
            tte.hash = hash;
            tte.move = 0xFFFF;
            tte.score = tt_store;
            tte.depth = 127; // Classifica como perfeito, profundidade inalcançável
            tte.flag = TT_EXACT;

            if (g_debug_arvore) {
                std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Poda EGTB score:" << tb_score << "\n";
            }
            return tb_score;
        }
    }

    if (depth <= 0) {
        int q_val = quiescence(pos, alpha, beta, 0, thread_id);
        if (g_debug_arvore) {
            std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Quiescence score:" << q_val << "\n";
        }
        return q_val;
    }

    DamasCore::MoveList moves = DamasCore::gerar_todas_capturas_maximais(pos);
    bool in_cap = !moves.empty();
    if (!in_cap) {
        DamasCore::BoardBitboard pieces = (pos.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? pos.obter_todas_pecas_brancas() : pos.obter_todas_pecas_pretas();
        while(pieces) {
            int sq = __builtin_ctzll(pieces);
            bool is_pawn = ((pos.obter_brancas_peoes() | pos.obter_pretas_peoes()) & (1ULL << sq));
            DamasCore::BoardBitboard dests = is_pawn ? DamasCore::gerar_movimentos_simples_peao(pos, sq) : DamasCore::gerar_movimentos_simples_dama(pos, sq);
            while(dests) { moves.push_back({sq, __builtin_ctzll(dests), 0}); dests &= dests - 1; }
            pieces &= pieces - 1;
        }
    }

    if (moves.empty()) return -SCORE_MATE + ply;

    bool is_pv = (beta - alpha > 1);

    sort_moves(moves, tt_move_compact, ply, thread_id);

    int best_score = -SCORE_INF;
    DamasCore::GameMove best_move = moves[0];

    for (size_t i = 0; i < moves.size(); ++i) {
        DamasCore::GameMove m = moves[i];
        
        if (g_debug_arvore) {
            std::string m_str = "";
            m_str += (char)('a' + (m.casa_origem % 8));
            m_str += (char)('8' - (m.casa_origem / 8));
            m_str += (m.mascaras_pecas_capturadas != 0) ? "x" : "-";
            m_str += (char)('a' + (m.casa_destino % 8));
            m_str += (char)('8' - (m.casa_destino / 8));
            std::cout << std::string(ply * 2, ' ') << " [AlphaBeta] Explorando lance " << (i+1) << "/" << moves.size() << ": " << m_str << "\n";
        }

        bool promotes = false;
        if (m.mascaras_pecas_capturadas == 0) {
            bool is_pawn = ((pos.obter_brancas_peoes() | pos.obter_pretas_peoes()) & (1ULL << m.casa_origem));
            promotes = is_pawn && ((1ULL << m.casa_destino) & (DamasCore::RANK_ULTIMA_BRANCA | DamasCore::RANK_PRIMEIRA_PRETA));
        }

        DamasCore::BoardState next = pos;
        if (m.mascaras_pecas_capturadas != 0) next.aplicar_movimento_completo_captura(m);
        else next.aplicar_mov_simples(m.casa_origem, m.casa_destino);
        next.alternar_turno();

        int val;
        // --- EXTENSÕES E REDUÇÕES (Inspirado no motor "Scan") ---
        int ext = 0;
        // Extensão Singular: Se só há um lance, é forçado. Analise mais a fundo.
        if (moves.size() == 1 && ply < 60) {
            ext = 1;
        }

        // LATE MOVE REDUCTION (LMR)
        // Se o lance não é uma captura, não promove, e não é um dos primeiros a serem tentados,
        // provavelmente é um lance ruim. Reduzimos a profundidade da busca para ele.
        int R = 0; // Redução
        if (depth >= 3 && i >= (is_pv ? 3 : 1) && !in_cap && !promotes) {
            R = 1;
            if (depth >= 6 && i >= (is_pv ? 5 : 3)) R = 2; // Redução maior para lances ainda mais tardios
        }
        
        if (i == 0) { // PVS: Janela Aberta
            val = -alpha_beta(next, depth - 1 + ext, -beta, -alpha, ply + 1, true, thread_id);
        } else { // Janela Fechada (Zero-Window)
            val = -alpha_beta(next, depth - 1 + ext - R, -alpha - 1, -alpha, ply + 1, true, thread_id);
            // Se a busca com profundidade reduzida superou alpha, o lance pode ser bom. RE-SEARCH com profundidade total.
            if (val > alpha && val < beta) val = -alpha_beta(next, depth - 1 + ext, -beta, -alpha, ply + 1, true, thread_id);
        }

        if (_stop) return 0;

        if (val > best_score) { best_score = val; best_move = m; }
        if (val > alpha) {
            alpha = val;
            if (alpha >= beta) {
                if (g_debug_arvore) {
                std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Poda Beta! score:" << val << " >= beta:" << beta << "\n";
                }
                if (!in_cap && ply < 128) {
                    _thread_data[thread_id].killers[ply][1] = _thread_data[thread_id].killers[ply][0]; _thread_data[thread_id].killers[ply][0] = m;
                    _thread_data[thread_id].history[m.casa_origem][m.casa_destino] += depth * depth;
                }
                break; // Corte Beta
            }
        }
    }

    int tt_store = best_score;
    if (tt_store > 19000) tt_store += ply;
    if (tt_store < -19000) tt_store -= ply;

    // A escrita na Tabela de Transposição (TT) é o ponto mais crítico de contenção.
    // A estratégia aqui é "last write wins" (o último a escrever vence).
    // Como a struct não é atômica, uma "escrita rasgada" (torn write) é possível, mas
    // a verificação do hash no início da função torna a leitura de dados corruptos improvável.
    tte.hash = hash;
    tte.move = (best_move.casa_origem & 0x3F) | ((best_move.casa_destino & 0x3F) << 6);
    tte.score = tt_store;
    tte.depth = depth;
    tte.flag = (best_score <= orig_alpha) ? TT_ALPHA : (best_score >= beta) ? TT_BETA : TT_EXACT;

    if (g_debug_arvore) {
        std::cout << std::string(ply * 2, ' ') << "[AlphaBeta] <- Retorno ply:" << ply << " best_score:" << best_score << "\n";
    }

    return best_score;
}

void DamasEngine::search(Search_Output& so, const DamasCore::BoardState& root, const Search_Input& si, int ply_count) {
    // --- PREPARAÇÃO ---
    clear_interrupt();
    _nodes.store(0, std::memory_order_relaxed);
    _start_time = std::chrono::steady_clock::now();
    _time_limit = si.time;
    
    // --- CONSULTA AO LIVRO DE ABERTURAS ---
    if (ply_count < 10) { // Apenas nos 10 primeiros lances
        DamasCore::GameMove book_move = consultar_livro(root, ply_count);
        if (book_move.casa_origem != -1) {
            so.move = book_move;
            so.score = 0; // Pontuação neutra para lances de livro
            so.depth = 100; // Profundidade especial para indicar que é do livro
            so.node = 1;
            so.pv.assign(1, book_move);
            {
                std::lock_guard<std::mutex> lock(_so_mutex);
                so.time_spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start_time).count();
                _current_so = so;
            }
            return; // Retorna imediatamente com o lance do livro
        }
    }

    // --- INTERCEPTAÇÃO DE APRENDIZADO DE GABARITO NA RAIZ ---
    // Se o lance ensinado (Memória Fotográfica Boa) for um dos lances possíveis na raiz, joga instantaneamente.
    DamasCore::MoveList root_moves = DamasCore::gerar_todas_capturas_maximais(root);
    if (root_moves.empty()) {
        DamasCore::BoardBitboard pieces = (root.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? root.obter_todas_pecas_brancas() : root.obter_todas_pecas_pretas();
        while(pieces) {
            int sq = __builtin_ctzll(pieces);
            bool is_pawn = ((root.obter_brancas_peoes() | root.obter_pretas_peoes()) & (1ULL << sq));
            DamasCore::BoardBitboard dests = is_pawn ? DamasCore::gerar_movimentos_simples_peao(root, sq) : DamasCore::gerar_movimentos_simples_dama(root, sq);
            while(dests) { root_moves.push_back({sq, __builtin_ctzll(dests), 0}); dests &= dests - 1; }
            pieces &= pieces - 1;
        }
    }

    for (size_t i = 0; i < root_moves.size(); ++i) {
        DamasCore::BoardState next = root;
        if (root_moves[i].mascaras_pecas_capturadas != 0) next.aplicar_movimento_completo_captura(root_moves[i]);
        else next.aplicar_mov_simples(root_moves[i].casa_origem, root_moves[i].casa_destino);
        next.alternar_turno();
        
        uint64_t h = next.obter_hash();
        // Se o lance leva a uma posição que é perdedora para o oponente,
        // então este é um lance vencedor e deve ser jogado instantaneamente.
        if (g_padroes_ruins.count(h)) {
            so.move = root_moves[i];
            so.score = SCORE_MATE - 1; // Pontuação de vitória forçada
            so.depth = 100; // Profundidade especial indicando gabarito exato
            so.node = 1;
            so.pv.clear();
            so.pv.push_back(root_moves[i]);
            {
                std::lock_guard<std::mutex> lock(_so_mutex);
                _current_so = so;
                _current_so.time_spent = 0.1; // Simula uma resposta instantânea
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Pausa sutil para atualizar a GUI
            so.time_spent = 0.1;
            return; // ABORTA a busca e força o lance imediatamente
        }
    }
    // --------------------------------------------------------

    // --- CHECAGEM DE EGTB (BASES DE FINAIS) NA RAIZ ---
    int total_pieces = DamasCore::contar_bits_ativos(root.obter_casas_ocupadas());
        if (total_pieces <= 6) {
        DamasCore::GameMove tb_move;
        int tb_score = 0, tb_dtm = 0;
        if (EGTB::probe_with_move(root, tb_move, tb_score, tb_dtm)) {
            so.move = tb_move;
            so.score = tb_score;
            so.depth = 100; // Profundidade especial para EGTB
            so.node = 1;
            so.pv.assign(1, tb_move);
            {
                std::lock_guard<std::mutex> lock(_so_mutex);
                so.time_spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start_time).count();
                _current_so = so;
            }
            return;
        }
        
    }

    // --- PREPARAÇÃO PARA A BUSCA ---
    if (!root_moves.empty()) {
        std::lock_guard<std::mutex> lock(_so_mutex);
        _current_so.move = root_moves[0];
        _current_so.score = evaluate(root);
        _current_so.depth = 1;
        _current_so.pv.assign(1, root_moves[0]);
        so = _current_so;
    }

    // Se houver apenas um lance, joga-o imediatamente.
    if (root_moves.size() == 1) {
        so.move = root_moves[0];
        so.score = evaluate(root);
        so.depth = 1; so.node = 1; so.pv.assign(1, root_moves[0]);
        {
            std::lock_guard<std::mutex> lock(_so_mutex);
            _current_so = so;
            _current_so.time_spent = 0.5;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        so.time_spent = 0.5;
        return;
    }
    
    // Reduz a importância dos lances históricos (aging) para todas as threads.
    for (auto& data : _thread_data) {
        for(int r=0; r<64; ++r) for(int c=0; c<64; ++c) data.history[r][c] >>= 1;
    }

    // --- LOOP DE APROFUNDAMENTO ITERATIVO (ITERATIVE DEEPENING) ---
    int prev_score = 0;
    for (int depth = 1; depth <= si.max_depth; ++depth) {
        // Janela de Aspiração: foca a busca em torno da pontuação da iteração anterior.
        int delta = 50;
        int alpha = (depth >= 3) ? std::max(-SCORE_INF, prev_score - delta) : -SCORE_INF;
        int beta  = (depth >= 3) ? std::min( SCORE_INF, prev_score + delta) :  SCORE_INF;

        while(true) {
            // --- INÍCIO DA BUSCA PARALELA (SMP) ---
            std::atomic<int> next_move_idx(0);
            std::vector<std::thread> workers;
            
            // Variáveis compartilhadas para esta iteração da busca
            std::atomic<int> shared_alpha(alpha);
            std::mutex best_move_mutex;
            DamasCore::GameMove current_best_move = root_moves[0];
            int best_score_this_iter = -SCORE_INF;

            // Função que cada thread irá executar
            auto worker_fn = [&](unsigned int thread_id) {
                while (!_stop) {
                    int move_idx = next_move_idx.fetch_add(1);
                    if (move_idx >= root_moves.size()) break;

                    DamasCore::GameMove m = root_moves[move_idx];
                    DamasCore::BoardState next = root;
                    if (m.mascaras_pecas_capturadas != 0) next.aplicar_movimento_completo_captura(m);
                    else next.aplicar_mov_simples(m.casa_origem, m.casa_destino);
                    next.alternar_turno();

                    int local_alpha = shared_alpha.load();
                    int score;
                    // Lógica PVS (Principal Variation Search)
                    if (move_idx == 0) { // Primeiro lance (melhor da iteração anterior) com janela cheia
                        score = -alpha_beta(next, depth - 1, -beta, -local_alpha, 1, false, thread_id);
                    } else { // Outros lances com janela nula (zero-window)
                        score = -alpha_beta(next, depth - 1, -local_alpha - 1, -local_alpha, 1, false, thread_id);
                        // Se a busca com janela nula falhar alto, re-busca com janela cheia
                        if (score > local_alpha && score < beta && !_stop) {
                            score = -alpha_beta(next, depth - 1, -beta, -local_alpha, 1, false, thread_id);
                        }
                    }

                    if (_stop) return;

                    // Atualiza o alpha compartilhado e o melhor lance encontrado
                    std::lock_guard<std::mutex> lock(best_move_mutex);
                    if (score > best_score_this_iter) {
                        best_score_this_iter = score;
                        current_best_move = m;
                        if (score > shared_alpha) shared_alpha.store(score);
                    }
                }
            };

            // Lança as threads trabalhadoras
            for (unsigned int i = 1; i < _num_threads; ++i) workers.emplace_back(worker_fn, i);
            worker_fn(0); // A thread principal também trabalha
            for (auto& t : workers) t.join(); // Espera todas terminarem

            int val = best_score_this_iter;
            if (_stop) return;
            
            // Lógica da Janela de Aspiração: se a busca falhou, ajusta a janela e tenta de novo.
            if (val <= alpha) {
                alpha = std::max(-SCORE_INF, val - delta);
                delta += delta / 2;
                continue;
            }
            if (val >= beta)  {
                beta = std::min(SCORE_INF, val + delta);
                delta += delta / 2;
                continue;
            }
            
            // --- FIM DA BUSCA PARALELA ---
            prev_score = val;
            std::lock_guard<std::mutex> lock(_so_mutex);
            _current_so.score = val;
            _current_so.depth = depth;
            _current_so.node = _nodes;
            _current_so.time_spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start_time).count();
            _current_so.move = current_best_move; // Atualiza com o melhor lance desta profundidade
            extract_pv(root, _current_so.pv);
            if (!_current_so.pv.empty()) {
                _current_so.move = _current_so.pv.front();
            }

            // --- NOVO: APRENDIZADO AUTOMÁTICO DE MATE ---
            // Se a busca encontrou um mate, salva a linha vencedora na memória fotográfica.
            if (val > SCORE_MATE - 100) {
                salvar_linha_vencedora(root, _current_so.pv);
            }
            // -------------------------------------------
            so = _current_so;
            break;
        }
        if (prev_score > 19000) break; // Early stop para vitória forçada
    }
}

// --- Ponte de Compatibilidade com a GUI ---

static double g_time_limit_seconds = 1.0;
static int g_max_depth_for_level = 8; // Profundidade padrão

void inicializar_ia() {
    inicializar_livro();
    carregar_pesos_ia();
}

void definir_nivel(int nivel) {
    switch (nivel) {
        case 0: g_time_limit_seconds = 1.0;  g_max_depth_for_level = 5; break;  // Iniciante
        case 1: g_time_limit_seconds = 2.0;  g_max_depth_for_level = 7; break;  // Experiente
        case 2: g_time_limit_seconds = 4.0;  g_max_depth_for_level = 9; break;  // Candidato
        case 3: g_time_limit_seconds = 10.0; g_max_depth_for_level = 13; break; // Mestre Nacional
        case 4: g_time_limit_seconds = 25.0; g_max_depth_for_level = 17; break; // Mestre Internacional
        case 5: g_time_limit_seconds = 60.0; g_max_depth_for_level = 21; break; // GMI
        default: g_time_limit_seconds = 1.0; g_max_depth_for_level = 5; break;
    }
}

DamasCore::GameMove encontrar_melhor_lance(const DamasCore::BoardState& estado_atual, int ply_count) {
    Search_Input si;
    si.time = g_time_limit_seconds;
    si.max_depth = g_max_depth_for_level;

    Search_Output so;
    search(so, estado_atual, si, ply_count);
    return so.move;
}

DamasCore::GameMove encontrar_melhor_lance_infinito(const DamasCore::BoardState& estado_atual, int ply_count) {
    Search_Input si;
    si.time = 3600 * 24; // 1 dia, efetivamente infinito
    si.max_depth = 100;

    Search_Output so;
    // No modo de análise, não usamos o livro, então passamos um ply_count alto.
    search(so, estado_atual, si, 99);
    return so.move;
}

} // namespace DamasAI