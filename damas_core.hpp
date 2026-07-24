/*
 * Damas Engine - Motor e Interface para Jogo de Damas Brasileiro
 * Copyright (C) 2024 Emanoel Libonati
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef DAMAS_CORE_HPP
#define DAMAS_CORE_HPP

#include <cstdint> // Para uint64_t
#include <vector>
#include <string>
#include <tuple>   // Para std::tuple na geração de movimentos de dama
#include <algorithm> // Para std::max
#include <random> // Para std::mt19937_64
#include <chrono> // Para std::chrono::steady_clock

namespace DamasCore {

/**
 * @enum GamePlayer
 * @brief Define os jogadores (ou cores) do jogo para a nova lógica.
 */
enum class GamePlayer {
    BRANCAS,
    PRETOS
};

// Alias para o tipo de 64 bits usado para os bitboards, para maior clareza.
using BoardBitboard = uint64_t;

// Máscaras para a primeira e última fileira (coroação) - adaptado para o novo namespace
constexpr BoardBitboard RANK_ULTIMA_BRANCA = 0x00000000000000FFULL; // Casas 0-7 (a8-h8) - Fileira de coroação para peças brancas
constexpr BoardBitboard RANK_PRIMEIRA_PRETA = 0xFF00000000000000ULL; // Casas 56-63 (a1-h1) - Fileira de coroação para peças pretas

// Máscaras de bits para as colunas 'a' e 'h' para evitar que as peças "deem a volta" no tabuleiro.
constexpr BoardBitboard BITBOARD_COLUNA_A = 0x0101010101010101ULL;
constexpr BoardBitboard BITBOARD_COLUNA_H = 0x8080808080808080ULL;
constexpr BoardBitboard BITBOARD_COLUNA_B = 0x0202020202020202ULL;
constexpr BoardBitboard BITBOARD_COLUNA_G = 0x4040404040404040ULL;

constexpr BoardBitboard NAO_COLUNA_A = ~BITBOARD_COLUNA_A;
constexpr BoardBitboard NAO_COLUNA_H = ~BITBOARD_COLUNA_H;

// Máscaras para movimentos duplos diagonais (capturas)
constexpr BoardBitboard NAO_COLUNA_A_OU_B = ~(BITBOARD_COLUNA_A | BITBOARD_COLUNA_B);
constexpr BoardBitboard NAO_COLUNA_H_OU_G = ~(BITBOARD_COLUNA_H | BITBOARD_COLUNA_G);

/**
 * @brief Conta o número de bits definidos (1s) em um BoardBitboard.
 * @param bb O BoardBitboard a ser contado.
 * @return O número de bits definidos.
 */
inline int contar_bits_ativos(BoardBitboard bb) {
    return __builtin_popcountll(bb);
}

// Zobrist Hashing: Chaves globais para cada peça em cada casa e para o turno
extern uint64_t ZOBRIST_KEYS[64][4]; // 64 casas, 4 tipos de peças (Peão Branco, Peão Preto, Dama Branca, Dama Preta)
extern uint64_t ZOBRIST_TURN; // Chave para o turno

void init_zobrist_keys(); // Função para inicializar as chaves Zobrist

/**
 * @struct GameMove
 * @brief Representa um movimento completo, que pode ser um simples avanço ou uma cadeia de captura.
 */
struct GameMove {
    int casa_origem = -1;           // Casa inicial da peça que se move. -1 para inválido.
    int casa_destino = -1;             // Casa final da peça. -1 para inválido.
    BoardBitboard mascaras_pecas_capturadas = 0; // Bitboard de todas as peças capturadas (0 se for um movimento simples)

    // Para comparação de movimentos, útil na IA e em testes
    bool operator==(const GameMove& other) const {
        return casa_origem == other.casa_origem &&
               casa_destino == other.casa_destino &&
               mascaras_pecas_capturadas == other.mascaras_pecas_capturadas;
    }

    bool operator!=(const GameMove& other) const {
        return !(*this == other);
    }
};

struct MoveList {
    GameMove lances[128];
    int count = 0;
    
    MoveList() : count(0) {}
    
    void clear() { count = 0; }
    void push_back(const GameMove& m) { if (count < 128) lances[count++] = m; }
    bool empty() const { return count == 0; }
    size_t size() const { return count; }
    const GameMove& operator[](size_t i) const { return lances[i]; }
    GameMove& operator[](size_t i) { return lances[i]; }
    
    GameMove* data() { return lances; }
    const GameMove* data() const { return lances; }
    
    GameMove* begin() { return lances; }
    GameMove* end() { return lances + count; }
    const GameMove* begin() const { return lances; }
    const GameMove* end() const { return lances + count; }
};

/**
 * @class BoardState
 * @brief Encapsula todo o estado do jogo usando bitboards, com nova nomenclatura.
 */
class BoardState {
public:
    // Construtor padrão para uma posição inicial vazia ou a ser configurada.
    BoardState()
        : _brancas_peoes(0),
          _pretas_peoes(0),
          _brancas_damas(0),
          _pretas_damas(0),
          _half_move_clock(0),
          _current_hash(0), // Inicializa o hash
          _turno_atual(GamePlayer::BRANCAS) {}

    /**
     * @brief Configura o tabuleiro para a posição inicial padrão do jogo de damas.
     */
    void configurar_posicao_inicial() {
        _brancas_peoes = 0;
        _pretas_peoes = 0;
        _brancas_damas = 0;
        _pretas_damas = 0;
        _current_hash = 0; // Resetar hash
        _turno_atual = GamePlayer::BRANCAS;
        _half_move_clock = 0;

        for (int linha = 0; linha < 3; ++linha) {
            for (int coluna = 0; coluna < 8; ++coluna) {
                if ((linha + coluna) % 2 != 0) {
                    _pretas_peoes |= (1ULL << (linha * 8 + coluna));
                }
            }
        }
        for (int linha = 5; linha < 8; ++linha) {
            for (int coluna = 0; coluna < 8; ++coluna) {
                if ((linha + coluna) % 2 != 0) {
                    _brancas_peoes |= (1ULL << (linha * 8 + coluna));
                }
            }
        }
        recalcular_hash_completo(); // Recalcular hash após configurar posição inicial
    }

    /**
     * @brief Define o estado do tabuleiro diretamente usando bitboards.
     * @param brancas_peoes_bb Bitboard dos peões brancos.
     * @param pretas_peoes_bb Bitboard dos peões pretos.
     * @param brancas_damas_bb Bitboard das damas brancas.
     * @param pretas_damas_bb Bitboard das damas pretas.
     * @param vez O turno do jogador.
     */
    void definir_posicao_via_bitboards(BoardBitboard brancas_peoes_bb, BoardBitboard pretas_peoes_bb,
                                       BoardBitboard brancas_damas_bb, BoardBitboard pretas_damas_bb, GamePlayer vez) {
        _brancas_peoes = brancas_peoes_bb;
        _pretas_peoes = pretas_peoes_bb;
        _brancas_damas = brancas_damas_bb;
        _pretas_damas = pretas_damas_bb;
        recalcular_hash_completo(); // Recalcular hash após definir via bitboards
        _turno_atual = vez;
        _half_move_clock = 0;
    }

    /**
     * @brief Limpa o tabuleiro, removendo todas as peças.
     */
    void limpar_tabuleiro() {
        _brancas_peoes = 0;
        _pretas_peoes = 0;
        _brancas_damas = 0;
        _pretas_damas = 0;
        _current_hash = 0; // Hash de tabuleiro limpo
        _half_move_clock = 0;
        _turno_atual = GamePlayer::BRANCAS;
    }

    // --- Funções de Acesso ---
    BoardBitboard obter_brancas_peoes() const { return _brancas_peoes; }
    BoardBitboard obter_pretas_peoes() const { return _pretas_peoes; }
    BoardBitboard obter_brancas_damas() const { return _brancas_damas; }
    BoardBitboard obter_pretas_damas() const { return _pretas_damas; }
    GamePlayer obter_turno_atual() const { return _turno_atual; }
    uint64_t obter_hash() const { return _current_hash; }
    int obter_relogio_meio_movimento() const { return _half_move_clock; }

    // --- Funções de Composição ---
    BoardBitboard obter_todas_pecas_brancas() const {
        return _brancas_peoes | _brancas_damas;
    }
    BoardBitboard obter_todas_pecas_pretas() const {
        return _pretas_peoes | _pretas_damas;
    }
    BoardBitboard obter_casas_ocupadas() const {
        return obter_todas_pecas_brancas() | obter_todas_pecas_pretas();
    }

    /**
     * @brief Aplica um movimento simples (não-captura) de uma casa para outra,
     *        e verifica coroação.
     */
    void aplicar_mov_simples(int casa_origem, int casa_destino) {
        // Atualiza o hash removendo a peça da origem e adicionando no destino
        const BoardBitboard mascara_origem = (1ULL << casa_origem);
        const BoardBitboard mascara_destino = (1ULL << casa_destino);

        if (((_brancas_peoes | _pretas_peoes) & mascara_origem) != 0) {
            _half_move_clock = 0; // Reset por movimento de peão
        } else {
            _half_move_clock++;
        }

        if ((_brancas_peoes & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[casa_origem][0]; // Remove peão branco da origem
            _brancas_peoes &= ~mascara_origem;
            if ((mascara_destino & RANK_ULTIMA_BRANCA) != 0) {
                _current_hash ^= ZOBRIST_KEYS[casa_destino][2]; // Adiciona dama branca no destino
                _brancas_damas |= mascara_destino;
            } else {
                _current_hash ^= ZOBRIST_KEYS[casa_destino][0]; // Adiciona peão branco no destino
                _brancas_peoes |= mascara_destino;
            }
        } else if ((_pretas_peoes & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[casa_origem][1]; // Remove peão preto da origem
            _pretas_peoes &= ~mascara_origem;
            if ((mascara_destino & RANK_PRIMEIRA_PRETA) != 0) {
                _current_hash ^= ZOBRIST_KEYS[casa_destino][3]; // Adiciona dama preta no destino
                _pretas_damas |= mascara_destino;
            } else {
                _current_hash ^= ZOBRIST_KEYS[casa_destino][1]; // Adiciona peão preto no destino
                _pretas_peoes |= mascara_destino;
            }
        } else if ((_brancas_damas & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[casa_origem][2]; // Remove dama branca da origem
            _current_hash ^= ZOBRIST_KEYS[casa_destino][2]; // Adiciona dama branca no destino
            _brancas_damas &= ~mascara_origem;
            _brancas_damas |= mascara_destino;
        } else if ((_pretas_damas & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[casa_origem][3]; // Remove dama preta da origem
            _current_hash ^= ZOBRIST_KEYS[casa_destino][3]; // Adiciona dama preta no destino
            _pretas_damas &= ~mascara_origem;
            _pretas_damas |= mascara_destino;
        }
        // O hash do turno será XORado em alternar_turno()
    }

    /**
     * @brief Aplica um movimento de captura em cadeia.
     *        Remove a peça de origem, adiciona a peça ao destino final,
     *        e remove todas as peças indicadas pela máscara de capturadas.
     *        Também verifica e aplica a coroação se a peça que se move
     *        for um peão e atingir a fileira de coroação.
     */
    void aplicar_movimento_completo_captura(const GameMove& mov_captura) {
        _half_move_clock = 0; // Reset por captura
        
        // Remove peças capturadas e atualiza o hash
        BoardBitboard temp_capturadas = mov_captura.mascaras_pecas_capturadas;
        while (temp_capturadas != 0) {
            int sq_capturada = __builtin_ctzll(temp_capturadas);
            if ((_brancas_peoes & (1ULL << sq_capturada)) != 0) {
                _current_hash ^= ZOBRIST_KEYS[sq_capturada][0];
            } else if ((_pretas_peoes & (1ULL << sq_capturada)) != 0) {
                _current_hash ^= ZOBRIST_KEYS[sq_capturada][1];
            } else if ((_brancas_damas & (1ULL << sq_capturada)) != 0) {
                _current_hash ^= ZOBRIST_KEYS[sq_capturada][2];
            } else if ((_pretas_damas & (1ULL << sq_capturada)) != 0) {
                _current_hash ^= ZOBRIST_KEYS[sq_capturada][3];
            }
            temp_capturadas &= temp_capturadas - 1;
        }
        // Remove todas as peças capturadas
        _brancas_peoes &= ~mov_captura.mascaras_pecas_capturadas;
        _pretas_peoes &= ~mov_captura.mascaras_pecas_capturadas;
        _brancas_damas &= ~mov_captura.mascaras_pecas_capturadas;
        _pretas_damas &= ~mov_captura.mascaras_pecas_capturadas;

        // Move a peça da origem para o destino e atualiza o hash
        const BoardBitboard mascara_origem = (1ULL << mov_captura.casa_origem);
        const BoardBitboard mascara_destino = (1ULL << mov_captura.casa_destino);

        bool era_peao = false;

        if ((_brancas_peoes & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_origem][0]; // Remove peão branco da origem
            _brancas_peoes &= ~mascara_origem;
            _brancas_peoes |= mascara_destino;
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][0]; // Adiciona peão branco no destino (temporariamente)
            era_peao = true;
        } else if ((_pretas_peoes & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_origem][1]; // Remove peão preto da origem
            _pretas_peoes &= ~mascara_origem;
            _pretas_peoes |= mascara_destino;
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][1]; // Adiciona peão preto no destino (temporariamente)
            era_peao = true;
        } else if ((_brancas_damas & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_origem][2]; // Remove dama branca da origem
            _brancas_damas &= ~mascara_origem;
            _brancas_damas |= mascara_destino;
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][2]; // Adiciona dama branca no destino
        } else if ((_pretas_damas & mascara_origem) != 0) {
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_origem][3]; // Remove dama preta da origem
            _pretas_damas &= ~mascara_origem;
            _pretas_damas |= mascara_destino;
            _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][3]; // Adiciona dama preta no destino
        }

        // Verifica a coroação se a peça movida era um peão
        if (era_peao) {
            if ((_brancas_peoes & mascara_destino) != 0 && (mascara_destino & RANK_ULTIMA_BRANCA) != 0) {
                _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][0]; // Remove peão branco
                _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][2]; // Adiciona dama branca
                _brancas_peoes &= ~mascara_destino;
                _brancas_damas |= mascara_destino;
            } else if ((_pretas_peoes & mascara_destino) != 0 && (mascara_destino & RANK_PRIMEIRA_PRETA) != 0) {
                _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][1]; // Remove peão preto
                _current_hash ^= ZOBRIST_KEYS[mov_captura.casa_destino][3]; // Adiciona dama preta
                _pretas_peoes &= ~mascara_destino;
                _pretas_damas |= mascara_destino;
            }
        }
    }

    /**
     * @brief Alterna o turno para o próximo jogador.
     */
    void alternar_turno() {
        _turno_atual = (_turno_atual == GamePlayer::BRANCAS) ? GamePlayer::PRETOS : GamePlayer::BRANCAS;
        _current_hash ^= ZOBRIST_TURN; // XOR com a chave do turno
    }

    // --- Funções para Montagem de Tabuleiro ---
    // Remove qualquer peça de uma casa e atualiza o hash
    void clearPieceAt(int square);
    void setPiece(int square, bool isKing, GamePlayer player);

    // --- Funções de Manipulação (para uso interno, ex: gerador de movimentos) ---
    void definir_brancas_peoes(BoardBitboard bb) { _brancas_peoes = bb; }
    void adicionar_brancas_peoes(BoardBitboard mascara) { _brancas_peoes |= mascara; }
    void remover_brancas_peoes(BoardBitboard mascara) { _brancas_peoes &= ~mascara; }

    void definir_pretas_peoes(BoardBitboard bb) { _pretas_peoes = bb; }
    void adicionar_pretas_peoes(BoardBitboard mascara) { _pretas_peoes |= mascara; }
    void remover_pretas_peoes(BoardBitboard mascara) { _pretas_peoes &= ~mascara; }

    void definir_brancas_damas(BoardBitboard bb) { _brancas_damas = bb; }
    void adicionar_brancas_damas(BoardBitboard mascara) { _brancas_damas |= mascara; }
    void remover_brancas_damas(BoardBitboard mascara) { _brancas_damas &= ~mascara; }

    void definir_pretas_damas(BoardBitboard bb) { _pretas_damas = bb; }
    void adicionar_pretas_damas(BoardBitboard mascara) { _pretas_damas |= mascara; }
    void remover_pretas_damas(BoardBitboard mascara) { _pretas_damas &= ~mascara; }

    void definir_turno_atual(GamePlayer vez) { _turno_atual = vez; }

public:
    // Recalcula o hash do zero (útil para inicialização ou carregamento de FEN)
    void recalcular_hash_completo() {
        _current_hash = 0;
        BoardBitboard temp_bb;
        temp_bb = _brancas_peoes; while (temp_bb != 0) { int sq = __builtin_ctzll(temp_bb); _current_hash ^= ZOBRIST_KEYS[sq][0]; temp_bb &= temp_bb - 1; }
        temp_bb = _pretas_peoes;   while (temp_bb != 0) { int sq = __builtin_ctzll(temp_bb); _current_hash ^= ZOBRIST_KEYS[sq][1]; temp_bb &= temp_bb - 1; }
        temp_bb = _brancas_damas;  while (temp_bb != 0) { int sq = __builtin_ctzll(temp_bb); _current_hash ^= ZOBRIST_KEYS[sq][2]; temp_bb &= temp_bb - 1; }
        temp_bb = _pretas_damas;   while (temp_bb != 0) { int sq = __builtin_ctzll(temp_bb); _current_hash ^= ZOBRIST_KEYS[sq][3]; temp_bb &= temp_bb - 1; }
        if (_turno_atual == GamePlayer::BRANCAS) {
            _current_hash ^= ZOBRIST_TURN;
        }
    }

private:
    BoardBitboard _brancas_peoes;
    BoardBitboard _pretas_peoes;
    BoardBitboard _brancas_damas;
    BoardBitboard _pretas_damas;
    int _half_move_clock; // Relógio de meios-movimentos para empates (Regra dos 20 lances)

    uint64_t _current_hash; // Hash Zobrist da posição atual

    GamePlayer _turno_atual;
};

// --- Declarações de Funções de Geração de Movimentos ---

// Gera movimentos simples (não-captura) para peões
BoardBitboard gerar_movimentos_simples_peao(const BoardState& estado, int casa_origem);

// Gera movimentos simples (não-captura) para damas
BoardBitboard gerar_movimentos_simples_dama(const BoardState& estado, int casa_origem);

// Função principal para gerar todas as sequências de captura maximais
MoveList gerar_todas_capturas_maximais(const BoardState& estado);

// Funções recursivas auxiliares (implementadas em damas_core.cpp)
void encontrar_sequencias_captura_peao_recursivo(
    BoardState estado_atual,
    int casa_peao_atual,
    int casa_origem_inicial,
    BoardBitboard mascara_capturadas_ate_agora,
    GameMove* todas_sequencias,
    int& count
);

void encontrar_sequencias_captura_dama_recursivo(
    BoardState estado_atual,
    int casa_dama_atual,
    int casa_origem_inicial,
    BoardBitboard mascara_capturadas_ate_agora,
    GameMove* todas_sequencias,
    int& count
);

} // namespace DamasCore

#endif // DAMAS_CORE_HPP