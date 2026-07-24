#ifndef CHECKERBOARD_H
#define CHECKERBOARD_H

#include <QWidget>
#include <QAction>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTimer>
#include "damas_ai.hpp" // Adicionado: Inclui o cabeçalho da IA para que DamasAI::Search_Output seja reconhecido
#include <QFutureWatcher>
#include "damas_core.hpp"

// Classe para o nosso widget de tabuleiro de damas
class CheckerBoard : public QWidget
{
    // Macro necessária para qualquer classe que use sinais/slots ou outras funcionalidades meta-objetos do Qt
    Q_OBJECT

public:
    // Construtor
    explicit CheckerBoard(QWidget *parent = nullptr);

    // Sobrescrevemos o método para sugerir um tamanho ideal para a janela
    QSize sizeHint() const override;

    // Enum para os modos de jogo
    enum class GameMode {
        HUMAN_VS_HUMAN,
        HUMAN_VS_AI,
        AI_VS_HUMAN,
        AI_VS_AI,
        ANALYSIS
    };

signals:
    // Sinal emitido quando o modo de montagem é ativado/desativado
    void setupModeChanged(bool inSetupMode);
    // Sinal emitido quando o modo de jogo é alterado programaticamente
    void gameModeChanged(CheckerBoard::GameMode newMode);

public slots:
    // Slot para reiniciar o jogo para a posição inicial
    void startNewGame();
    // Slot para salvar o estado atual do jogo em um arquivo .pdn
    void saveGame();
    // Slot para carregar um jogo de um arquivo .pdn
    void openGame();
    // Slot para carregar uma posição de um lote em um arquivo .pdn
    void openBatch();
    // Slot para salvar a posição atual em um arquivo .fen
    void savePosition();
    // Slot para carregar uma posição de um arquivo .fen
    void openPosition();
    // Slot para entrar no modo de montagem do tabuleiro
    void enterSetupMode();
    // Slot para redimensionar o tabuleiro
    void setScale(double scale);
    // Slot para girar o tabuleiro
    void rotateBoard();
    // Slots para navegar no histórico de jogadas
    void undoMove();
    void redoMove();

    // Define as ações de menu para que o widget possa habilitá-las/desabilitá-las
    void setUndoRedoActions(QAction* undo, QAction* redo);

    // Slots para definir o modo de jogo e o nível da IA
    void setGameMode(GameMode mode);
    void setAiLevel(int level);
    // Slot para ensinar a IA a corrigir um erro
    void correctAiMove();
    // Slot para adicionar o lance atual ao livro de aberturas
    void addCurrentMoveToBook();

protected:
    // O evento de pintura. Todo o desenho acontece aqui.
    void paintEvent(QPaintEvent *event) override;
    // O evento de clique do mouse. A interação do usuário começa aqui.
    void mousePressEvent(QMouseEvent *event) override;
    // O evento de redimensionamento, para ajustar widgets filhos.
    void resizeEvent(QResizeEvent *event) override;

private slots:
    // Slot para reagir à mudança do modo de montagem
    void onSetupModeChanged(bool inSetupMode);
    // Slot para atualizar as informações de análise da IA periodicamente
    void updateAnalysisInfo();
    // Funções para controlar a análise infinita
    void startInfiniteAnalysis();
    void stopAnalysisAndPlayBestMove();
    // Slot para receber o resultado da busca da IA quando ela terminar
    void onAiMoveFinished();

private:
    // Ferramentas disponíveis na paleta de montagem
    enum class SetupTool {
        NONE,
        ERASER,
        WHITE_PAWN,
        BLACK_PAWN,
        WHITE_KING,
        BLACK_KING
    };

    // Enum para a GUI representar as peças, incluindo damas (reis)
    enum class Piece {
        NONE,
        WHITE_PAWN,
        BLACK_PAWN,
        WHITE_KING,
        BLACK_KING
    };

    void updateDimensions(double scale);
    void setupInitialPosition();
    Piece getPieceAt(int row, int col) const;
    void calculatePossibleMoves();
    void updateMovesForSelection(int square);
    void executeMove(const DamasCore::GameMove& move, bool is_ai_move = false);
    void initializePaletteRects();
    QString generateFen() const { return generateFen(m_coreState); }
    QString generateFen(const DamasCore::BoardState& state) const;
    QString squareToAlgebraic(int square) const;
    QString moveToAlgebraic(const DamasCore::GameMove& move) const;
    bool loadFromFen(const QString& fen);
    void applyMovesFromPdn(const QString& moveText);
    void drawAnalysisPanel(QPainter& painter);
    void drawSetupPalette(QPainter& painter);
    void updateMoveListWidget();
    void triggerAiMove();
    void resetHistory();
    void updateUndoRedoActions();
    void checkGameOver();

    // Dimensões do tabuleiro (não constantes para permitir redimensionamento)
    int m_squareSize;
    int m_marginSize;

    // Constantes de classe para o design do tabuleiro
    static constexpr int BoardSize = 8;        // 8x8
    static constexpr int PanelWidthInSquares = 3; // Largura do painel lateral em casas
    
    // Instância do motor do jogo que contém o estado do tabuleiro
    DamasCore::BoardState m_coreState;

    // Estado da UI para interação
    int m_selectedSquare = -1; // Casa selecionada pelo usuário em modo de jogo (-1 se nenhuma)
    DamasCore::MoveList m_allPossibleMoves; // Todos os lances legais para o jogador atual
    DamasCore::MoveList m_movesForSelectedPiece; // Lances filtrados para a peça selecionada

    // Histórico de jogadas para Voltar/Avançar
    std::vector<DamasCore::BoardState> m_history;
    std::vector<DamasCore::GameMove> m_moveHistory;
    int m_historyIndex = -1;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    // Estado do modo de jogo e IA
    GameMode m_gameMode = GameMode::HUMAN_VS_HUMAN;
    bool m_isAiThinking = false; // Flag para bloquear a UI sem desabilitar o widget
    bool m_isCorrectingMove = false; // Flag para o novo fluxo de correção de lance
    DamasCore::BoardState m_correctionStateBad; // Armazena o estado "ruim" durante a correção
    bool m_isGameOver = false;
    bool m_playMoveOnAnalysisFinish = false; // Flag para indicar que o lance da análise deve ser jogado

    // Análise da IA
    DamasAI::Search_Output m_currentAnalysisInfo;
    QTimer* m_analysisUpdateTimer;

    // Widget para exibir a lista de lances
    QPlainTextEdit* m_moveListWidget;

    // Estado e ferramentas para o modo de montagem
    bool m_isBoardRotated = false;
    bool m_isSetupMode = false;
    bool m_isClearButtonInClearState = true; // Controla o estado do botão Limpar/Pos. Inicial
    SetupTool m_setupTool = SetupTool::ERASER;
    QRect m_paletteRect;
    QRect m_eraserToolRect;
    QRect m_whitePawnToolRect;
    QRect m_blackPawnToolRect;
    QRect m_whiteKingToolRect;
    QRect m_blackKingToolRect;
    QRect m_clearBoardButtonRect;
    QRect m_playerTurnButtonRect;
    QRect m_doneButtonRect;

    // Watcher para monitorar a busca da IA em um thread separado
    QFutureWatcher<DamasCore::GameMove> m_aiMoveWatcher;
};

#endif // CHECKERBOARD_H
