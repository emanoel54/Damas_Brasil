#include "damas_core.hpp"
#include <tuple>

namespace DamasCore {

// Definição e inicialização das chaves Zobrist globais
uint64_t ZOBRIST_KEYS[64][4];
uint64_t ZOBRIST_TURN;

void init_zobrist_keys() {
    // Usar uma Seed Fixa garante que a Memória Fotográfica (Hashes) seja eterna e funcione após reiniciar o programa
    std::mt19937_64 rng(0x123456789ABCDEF0ULL); 
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 4; ++j) {
            ZOBRIST_KEYS[i][j] = rng();
        }
    }
    ZOBRIST_TURN = rng();
}

// --- Implementação das Funções de Geração de Movimentos ---

BoardBitboard gerar_movimentos_simples_peao(const BoardState& estado, int casa_origem) {
    const BoardBitboard casas_vazias = ~estado.obter_casas_ocupadas();
    const BoardBitboard mascara_peao = (1ULL << casa_origem);
    BoardBitboard movimentos_possiveis = 0;

    bool eh_peao_branco = (estado.obter_brancas_peoes() & mascara_peao) != 0;

    if (eh_peao_branco) {
        // Movimentos para "cima" no tabuleiro (índices menores)
        BoardBitboard avanco_esquerda = (mascara_peao & NAO_COLUNA_A) >> 9; // UL
        BoardBitboard avanco_direita = (mascara_peao & NAO_COLUNA_H) >> 7; // UR
        movimentos_possiveis = (avanco_esquerda | avanco_direita) & casas_vazias;
    } else { // GamePlayer::PRETOS
        // Movimentos para "baixo" no tabuleiro (índices maiores)
        BoardBitboard avanco_esquerda = (mascara_peao & NAO_COLUNA_A) << 7; // DL
        BoardBitboard avanco_direita = (mascara_peao & NAO_COLUNA_H) << 9; // DR
        movimentos_possiveis = (avanco_esquerda | avanco_direita) & casas_vazias;
    }
    return movimentos_possiveis;
}

BoardBitboard gerar_movimentos_simples_dama(const BoardState& estado, int casa_origem) {
    BoardBitboard movimentos_possiveis = 0;
    const BoardBitboard casas_ocupadas = estado.obter_casas_ocupadas();

    // Direções: {deslocamento, máscara de borda}
    const std::pair<int, BoardBitboard> direcoes[4] = {
        {-9, NAO_COLUNA_A}, // Cima-Esquerda
        {-7, NAO_COLUNA_H}, // Cima-Direita
        {+7, NAO_COLUNA_A}, // Baixo-Esquerda
        {+9, NAO_COLUNA_H}  // Baixo-Direita
    };

    for (const auto& dir : direcoes) {
        int deslocamento = dir.first;
        BoardBitboard mascara_borda = dir.second;

        for (int n = 1; n < 8; ++n) {
            int casa_anterior = casa_origem + (n - 1) * deslocamento;
            // Verifica se o movimento anterior já estava na borda
            if (((1ULL << casa_anterior) & mascara_borda) == 0) {
                break;
            }
            
            int casa_alvo = casa_origem + n * deslocamento;
            if (casa_alvo < 0 || casa_alvo > 63) {
                break;
            }
            BoardBitboard mascara_alvo = (1ULL << casa_alvo);

            if ((casas_ocupadas & mascara_alvo) == 0) {
                movimentos_possiveis |= mascara_alvo; // Casa vazia, adiciona e continua
            } else {
                break; // Atingiu uma peça, para nesta direção
            }
        }
    }
    return movimentos_possiveis;
}

// --- Lógica de Captura ---

void encontrar_sequencias_captura_peao_recursivo(
    BoardState estado_atual,
    int casa_peao_atual,
    int casa_origem_inicial,
    BoardBitboard mascara_capturadas_ate_agora,
    GameMove* todas_sequencias,
    int& count
) {
    bool encontrou_captura_neste_passo = false;
    const GamePlayer jogador_atual = estado_atual.obter_turno_atual();
    const BoardBitboard pecas_oponentes = (jogador_atual == GamePlayer::BRANCAS) ? estado_atual.obter_todas_pecas_pretas() : estado_atual.obter_todas_pecas_brancas();
    const BoardBitboard mascara_peao_atual = (1ULL << casa_peao_atual);

    // Direções de captura para peões: {delta_para_oponente, mascara_origem_para_oponente, mascara_oponente_para_pouso}
    // delta_para_oponente: deslocamento da casa atual para a casa do oponente
    // mascara_origem_para_oponente: máscara para verificar se a casa atual está na borda para o deslocamento do oponente
    // mascara_oponente_para_pouso: máscara para verificar se a casa do oponente está na borda para o deslocamento de pouso
    std::tuple<int, BoardBitboard, BoardBitboard> direcoes_captura_peao[4];

    if (jogador_atual == GamePlayer::BRANCAS) {
        // Brancas: UL, UR (para frente), DL, DR (para trás)
        direcoes_captura_peao[0] = std::make_tuple(-9, NAO_COLUNA_A, NAO_COLUNA_A); // UL
        direcoes_captura_peao[1] = std::make_tuple(-7, NAO_COLUNA_H, NAO_COLUNA_H); // UR
        direcoes_captura_peao[2] = std::make_tuple(+7, NAO_COLUNA_A, NAO_COLUNA_A); // DL
        direcoes_captura_peao[3] = std::make_tuple(+9, NAO_COLUNA_H, NAO_COLUNA_H); // DR
    } else { // GamePlayer::PRETOS
        // Pretas: DL, DR (para frente), UL, UR (para trás)
        direcoes_captura_peao[0] = std::make_tuple(+7, NAO_COLUNA_A, NAO_COLUNA_A); // DL
        direcoes_captura_peao[1] = std::make_tuple(+9, NAO_COLUNA_H, NAO_COLUNA_H); // DR
        direcoes_captura_peao[2] = std::make_tuple(-9, NAO_COLUNA_A, NAO_COLUNA_A); // UL
        direcoes_captura_peao[3] = std::make_tuple(-7, NAO_COLUNA_H, NAO_COLUNA_H); // UR
    }

    for (const auto& dir : direcoes_captura_peao) {
        int delta = std::get<0>(dir);
        BoardBitboard mascara_origem_oponente = std::get<1>(dir);
        BoardBitboard mascara_oponente_pouso = std::get<2>(dir);

        // Verifica se a peça atual está na borda para o movimento do oponente
        if (!((mascara_peao_atual & mascara_origem_oponente) != 0)) continue;

        int casa_oponente = casa_peao_atual + delta;
        int casa_pouso = casa_oponente + delta;

        // Verifica se as casas estão dentro dos limites do tabuleiro
        if (casa_oponente < 0 || casa_oponente > 63 || casa_pouso < 0 || casa_pouso > 63) continue;

        BoardBitboard mascara_oponente = (1ULL << casa_oponente);
        BoardBitboard mascara_pouso = (1ULL << casa_pouso);

        // Verifica se a casa do oponente está na borda para o movimento de pouso
        if (!((mascara_oponente & mascara_oponente_pouso) != 0)) continue;

        if ((mascara_oponente & pecas_oponentes) != 0 && (mascara_pouso & ~estado_atual.obter_casas_ocupadas()) != 0 && (mascara_oponente & ~mascara_capturadas_ate_agora) != 0) {
            encontrou_captura_neste_passo = true; // Use the unqualified name for recursion within the same namespace
            BoardState proximo_estado = estado_atual;
            proximo_estado.aplicar_movimento_completo_captura({casa_peao_atual, casa_pouso, mascara_oponente});
            encontrar_sequencias_captura_peao_recursivo(proximo_estado, casa_pouso, casa_origem_inicial, mascara_capturadas_ate_agora | mascara_oponente, todas_sequencias, count);
        }
    }


    if (!encontrou_captura_neste_passo && mascara_capturadas_ate_agora != 0) {
        if (count < 128) {
            todas_sequencias[count++] = {casa_origem_inicial, casa_peao_atual, mascara_capturadas_ate_agora};
        }
    }
}

void encontrar_sequencias_captura_dama_recursivo(
    BoardState estado_atual,
    int casa_dama_atual,
    int casa_origem_inicial,
    BoardBitboard mascara_capturadas_ate_agora,
    GameMove* todas_sequencias,
    int& count
) {
    bool encontrou_captura_neste_passo = false;
    const GamePlayer jogador_atual = estado_atual.obter_turno_atual();
    const BoardBitboard pecas_oponentes = (jogador_atual == GamePlayer::BRANCAS) ? estado_atual.obter_todas_pecas_pretas() : estado_atual.obter_todas_pecas_brancas();
    // Para a lógica da Dama, as casas onde as peças já foram capturadas nesta sequência
    // devem ser tratadas como ocupadas para bloquear a linha de visão.
    const BoardBitboard casas_ocupadas_para_dama = estado_atual.obter_casas_ocupadas() | mascara_capturadas_ate_agora;

    const std::pair<int, BoardBitboard> direcoes[4] = { {-9, NAO_COLUNA_A}, {-7, NAO_COLUNA_H}, {+7, NAO_COLUNA_A}, {+9, NAO_COLUNA_H} };

    for (const auto& dir : direcoes) {
        int deslocamento = dir.first;
        BoardBitboard mascara_borda = dir.second;
        
        for (int n = 1; n < 8; ++n) {
            int casa_anterior = casa_dama_atual + (n - 1) * deslocamento;
            if (((1ULL << casa_anterior) & mascara_borda) == 0) break;

            int casa_oponente_potencial = casa_dama_atual + n * deslocamento;
            if (casa_oponente_potencial < 0 || casa_oponente_potencial > 63) break;
            BoardBitboard mascara_oponente_potencial = (1ULL << casa_oponente_potencial);

            if ((mascara_oponente_potencial & pecas_oponentes) != 0 && (mascara_oponente_potencial & ~mascara_capturadas_ate_agora) != 0) {
                for (int m = n + 1; m < 8; ++m) {
                    int casa_pouso_anterior = casa_dama_atual + (m - 1) * deslocamento;
                    if (((1ULL << casa_pouso_anterior) & mascara_borda) == 0) break;

                    int casa_pouso_potencial = casa_dama_atual + m * deslocamento;
                    if (casa_pouso_potencial < 0 || casa_pouso_potencial > 63) break; // Corrigido para usar a máscara combinada
                    BoardBitboard mascara_pouso_potencial = (1ULL << casa_pouso_potencial);

                    if ((mascara_pouso_potencial & casas_ocupadas_para_dama) != 0) break;

                    encontrou_captura_neste_passo = true;
                    BoardState proximo_estado = estado_atual; // Use the unqualified name for recursion within the same namespace
                    proximo_estado.aplicar_movimento_completo_captura({casa_dama_atual, casa_pouso_potencial, mascara_oponente_potencial});
                    encontrar_sequencias_captura_dama_recursivo(proximo_estado, casa_pouso_potencial, casa_origem_inicial, mascara_capturadas_ate_agora | mascara_oponente_potencial, todas_sequencias, count);
                }
                break;
            }
            if ((mascara_oponente_potencial & casas_ocupadas_para_dama) != 0) break;
        }
    }

    if (!encontrou_captura_neste_passo && mascara_capturadas_ate_agora != 0) {
        if (count < 128) {
            todas_sequencias[count++] = {casa_origem_inicial, casa_dama_atual, mascara_capturadas_ate_agora};
        }
    }
}

MoveList gerar_todas_capturas_maximais(const BoardState& estado) {
    GameMove todas_as_capturas_possiveis[128];
    int count = 0;
    const GamePlayer jogador_atual = estado.obter_turno_atual();

    BoardBitboard peoes_para_verificar = (jogador_atual == GamePlayer::BRANCAS) ? estado.obter_brancas_peoes() : estado.obter_pretas_peoes();
    while (peoes_para_verificar != 0) {
        int casa_origem = __builtin_ctzll(peoes_para_verificar);
        encontrar_sequencias_captura_peao_recursivo(estado, casa_origem, casa_origem, 0ULL, todas_as_capturas_possiveis, count);
        peoes_para_verificar &= peoes_para_verificar - 1;
    }

    BoardBitboard damas_para_verificar = (jogador_atual == GamePlayer::BRANCAS) ? estado.obter_brancas_damas() : estado.obter_pretas_damas();
    while (damas_para_verificar != 0) {
        int casa_origem = __builtin_ctzll(damas_para_verificar);
        encontrar_sequencias_captura_dama_recursivo(estado, casa_origem, casa_origem, 0ULL, todas_as_capturas_possiveis, count);
        damas_para_verificar &= damas_para_verificar - 1;
    }

    if (count == 0) return MoveList();

    int max_capturas = 0;
    for (int i = 0; i < count; ++i) {
        const auto& mov = todas_as_capturas_possiveis[i];
        max_capturas = std::max(max_capturas, contar_bits_ativos(mov.mascaras_pecas_capturadas));
    }

    MoveList maximais;
    for (int i = 0; i < count; ++i) {
        const auto& mov = todas_as_capturas_possiveis[i];
        if (contar_bits_ativos(mov.mascaras_pecas_capturadas) == max_capturas) {
            maximais.push_back(mov);
        }
    }
    return maximais;
}

// --- Implementação das Funções de Montagem ---

void BoardState::clearPieceAt(int square) {
    const BoardBitboard mask = (1ULL << square);
    if ((_brancas_peoes & mask) != 0) { _current_hash ^= ZOBRIST_KEYS[square][0]; _brancas_peoes &= ~mask; }
    else if ((_pretas_peoes & mask) != 0) { _current_hash ^= ZOBRIST_KEYS[square][1]; _pretas_peoes &= ~mask; }
    else if ((_brancas_damas & mask) != 0) { _current_hash ^= ZOBRIST_KEYS[square][2]; _brancas_damas &= ~mask; }
    else if ((_pretas_damas & mask) != 0) { _current_hash ^= ZOBRIST_KEYS[square][3]; _pretas_damas &= ~mask; }
}

void BoardState::setPiece(int square, bool isKing, GamePlayer player) {
    clearPieceAt(square); // Garante que a casa esteja vazia e o hash atualizado
    const BoardBitboard mask = (1ULL << square);

    if (player == GamePlayer::BRANCAS) {
        if (isKing) {
            _brancas_damas |= mask;
            _current_hash ^= ZOBRIST_KEYS[square][2];
        } else {
            _brancas_peoes |= mask;
            _current_hash ^= ZOBRIST_KEYS[square][0];
        }
    } else { // PRETOS
        if (isKing) {
            _pretas_damas |= mask;
            _current_hash ^= ZOBRIST_KEYS[square][3];
        } else {
            _pretas_peoes |= mask;
            _current_hash ^= ZOBRIST_KEYS[square][1];
        }
    }
}


} // namespace DamasCore