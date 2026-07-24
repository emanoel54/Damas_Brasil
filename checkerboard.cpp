#include "checkerboard.h"
#include <QPainter>
#include <QDebug>
#include <QPaintEvent>
#include <QColor>
#include <QFont>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QResizeEvent>
#include <QInputDialog>
#include <QRegularExpression>
#include <QMessageBox>
#include <QProcess>
#include <QCoreApplication>
#include "damas_ai.hpp"
#include <QMenu>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QKeyEvent>
#include <functional>
#include <QtConcurrent/QtConcurrent>

namespace {
    // Cores do tabuleiro para fácil customização
    const QColor LightSquareColor = QColor("#F0D9B5");
    const QColor DarkSquareColor = QColor("#B58863");
    const QColor PanelColor = QColor("#E0E0E0"); // Cor para o painel lateral

    // Dimensões base para o redimensionamento
    const int BaseSquareSize = 60;
    const int BaseMarginSize = 30;
}

// Classe de filtro de eventos para lidar com o pressionamento da tecla Delete
class DeleteKeyFilter : public QObject {
public:
    // Passamos uma lambda para ser executada ao deletar.
    DeleteKeyFilter(std::function<void()> onDeleteFunc, QObject* parent)
        : QObject(parent), onDelete(onDeleteFunc) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Delete) {
                onDelete();
                return true; // Nós lidamos com o evento.
            }
        }
        // Passa outros eventos adiante.
        return QObject::eventFilter(obj, event);
    }
private:
    std::function<void()> onDelete;
};

// Função auxiliar para criar e configurar um diálogo de arquivo com funcionalidade de exclusão.
// Retorna o nome do arquivo selecionado ou uma string vazia se cancelado.
QString getFileNameWithDelete(QWidget* parent, const QString& caption, const QString& dir, const QString& filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    // A função a ser chamada para deletar o arquivo atualmente selecionado.
    auto deleteSelectedFile = [&dialog]() {
        QStringList selectedFiles = dialog.selectedFiles();
        if (selectedFiles.isEmpty()) {
            return;
        }
        QString fileToDelete = selectedFiles.first();

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(&dialog, "Confirmar Exclusão",
                                      QString("Tem certeza que deseja deletar o arquivo '%1'?").arg(QFileInfo(fileToDelete).fileName()),
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (!QFile::remove(fileToDelete)) {
                QMessageBox::warning(&dialog, "Erro", QString("Não foi possível deletar o arquivo '%1'.").arg(QFileInfo(fileToDelete).fileName()));
            }
            // O QFileSystemModel usado pelo diálogo deve detectar automaticamente
            // a remoção do arquivo e atualizar a visualização. Nenhuma atualização manual deve ser necessária.
        }
    };

    // Encontra o widget de visualização principal para anexar interações.
    QAbstractItemView* mainView = nullptr;
    // A visualização principal é geralmente um QListView ou um QTreeView.
    auto views = dialog.findChildren<QAbstractItemView*>();
    for (auto* view : views) {
        // Uma heurística simples: a barra lateral tem um nome de objeto específico. Queremos a outra visualização.
        if (view->objectName() != "sidebar") {
            mainView = view;
            break;
        }
    }

    if (mainView) {
        // 1. Habilita o menu de contexto para interação com o mouse.
        mainView->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(mainView, &QWidget::customContextMenuRequested, mainView, [mainView, deleteSelectedFile](const QPoint &pos){
            // Mostra o menu apenas se um item válido for clicado.
            if (mainView->indexAt(pos).isValid()) {
                QMenu menu;
                QAction* deleteAction = menu.addAction("Deletar");
                QObject::connect(deleteAction, &QAction::triggered, deleteSelectedFile);
                menu.exec(mainView->viewport()->mapToGlobal(pos));
            }
        });

        // 2. Instala um filtro de eventos para a tecla Delete.
        mainView->installEventFilter(new DeleteKeyFilter(deleteSelectedFile, mainView));
    }

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.selectedFiles().value(0);
    }

    return QString(); // Usuário cancelou
}

CheckerBoard::CheckerBoard(QWidget *parent)
    : QWidget(parent)
{   
    updateDimensions(1.0); // Define o tamanho inicial (escala 1.0)
    initializePaletteRects();

    // Cria e configura o widget da lista de lances
    m_moveListWidget = new QPlainTextEdit(this);
    m_moveListWidget->setReadOnly(true);
    m_moveListWidget->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Define a largura da barra de rolagem e permite copiar com o mouse/teclado
    m_moveListWidget->setStyleSheet("QScrollBar:vertical { width: 6px; } QScrollBar:horizontal { height: 6px; }");
    m_moveListWidget->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    
    // Adia a configuração da fonte para evitar o erro QFontDatabase
    QTimer::singleShot(0, this, [this]() {
        QFont movesFont("Monospace");
        movesFont.setStyleHint(QFont::TypeWriter);
        m_moveListWidget->setFont(movesFont);
    });

    setupInitialPosition();
    resetHistory(); // Inicia o histórico com a posição inicial
    calculatePossibleMoves();
    updateMoveListWidget(); // Atualiza a lista de lances (que estará vazia)
    setAiLevel(0); // Define um nível padrão

    // Conecta o sinal para esconder/mostrar a lista de lances
    connect(this, &CheckerBoard::setupModeChanged, this, &CheckerBoard::onSetupModeChanged);

    // Conecta o watcher para receber o lance da IA quando a busca em background terminar
    connect(&m_aiMoveWatcher, &QFutureWatcher<DamasCore::GameMove>::finished, this, &CheckerBoard::onAiMoveFinished);

    // Cria e configura o timer para atualizar a análise da IA
    m_analysisUpdateTimer = new QTimer(this);
    connect(m_analysisUpdateTimer, &QTimer::timeout, this, &CheckerBoard::updateAnalysisInfo);
    m_analysisUpdateTimer->setInterval(100); // Atualiza 10 vezes por segundo

}

QSize CheckerBoard::sizeHint() const
{
    const int totalHeight = 2 * m_marginSize + BoardSize * m_squareSize;
    const int totalWidth = 2 * m_marginSize + (BoardSize + PanelWidthInSquares) * m_squareSize;
    return QSize(totalWidth, totalHeight);
}

void CheckerBoard::startNewGame()
{
    // Reseta o estado da UI para garantir que não haja seleções antigas
    m_selectedSquare = -1;
    m_isGameOver = false;
    m_movesForSelectedPiece.clear();

    // Reinicia o estado do jogo para a posição inicial
    setupInitialPosition();
    resetHistory();

    // Calcula os movimentos para o primeiro jogador (brancas)
    calculatePossibleMoves();
    updateMoveListWidget();
    triggerAiMove(); // Verifica se a IA deve começar

    // Solicita um redesenho completo da tela
    update();
}

void CheckerBoard::saveGame()
{
    // Abre um diálogo para o usuário escolher onde salvar o arquivo
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Salvar Jogo"), "",
        tr("Portable Draughts Notation (*.pdn);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; // O usuário cancelou
    }

    // Garante que a extensão seja .pdn se o usuário não a digitou
    if (!fileName.endsWith(".pdn", Qt::CaseInsensitive)) {
        fileName += ".pdn";
    }

    QFile file(fileName);
    if (!file.open(QIODeviceBase::WriteOnly)) {
        // TODO: Mostrar uma mensagem de erro para o usuário
        return;
    }

    QTextStream out(&file);
    // O FEN deve representar a posição inicial do jogo.
    if (!m_history.empty()) {
        out << generateFen(m_history[0]) << Qt::endl;
    } else {
        out << generateFen(m_coreState) << Qt::endl; // Fallback
    }
    out << Qt::endl;

    // Monta a string com a lista de lances para o arquivo

    // Verifica se o jogo começou com as brancas ou pretas
    bool white_started = true;
    if (!m_history.empty()) {
        white_started = (m_history[0].obter_turno_atual() == DamasCore::GamePlayer::BRANCAS);
    }

    int move_idx = 0;
    int move_number = 1;
    QStringList move_list_for_file;

    // Se as pretas começaram, o primeiro lance é tratado de forma especial
    if (!white_started && m_historyIndex > 0) {
        QString black_first_move = QString("1. ... %1").arg(moveToAlgebraic(m_moveHistory[0]));
        move_list_for_file.append(black_first_move);
        move_idx = 1;
        move_number = 2;
    }

    // Itera sobre os pares de lances restantes
    for (/* inicialização feita acima */; move_idx < m_historyIndex; move_idx += 2, move_number++) {
        QString line = QString("%1. %2").arg(move_number).arg(moveToAlgebraic(m_moveHistory[move_idx]));

        // Adiciona o lance das pretas se houver
        if (move_idx + 1 < m_historyIndex) {
            line += " " + moveToAlgebraic(m_moveHistory[move_idx + 1]);
        }
        move_list_for_file.append(line);
    }

    // Escreve a lista de lances, separados por espaço, em uma única linha.
    out << move_list_for_file.join(" ") << Qt::endl;
}

void CheckerBoard::openGame()
{
    QString fileName = getFileNameWithDelete(this,
        tr("Abrir Jogo"), "",
        tr("Portable Draughts Notation (*.pdn);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; // O usuário cancelou
    }

    QFile file(fileName);
    if (!file.open(QIODeviceBase::ReadOnly)) {
        // TODO: Mostrar uma mensagem de erro para o usuário
        return;
    }

    QTextStream in(&file);
    QString fenLine;
    QString moveLine;

    // Lê a linha FEN
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[FEN")) {
            fenLine = line;
            break;
        }
    }

    // Lê a linha de lances (deve ser a próxima linha não vazia)
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty()) {
            moveLine = line;
            break;
        }
    }

    // Tenta carregar a posição a partir do FEN. Isso é crucial para posições customizadas.
    if (loadFromFen(fenLine)) {
        // O FEN foi carregado com sucesso. A função loadFromFen já
        // configurou o estado inicial e o histórico.
        // Agora, aplica todos os lances do arquivo para construir o histórico completo.
        applyMovesFromPdn(moveLine);
    } else {
        // Se o FEN falhar (formato inválido ou arquivo antigo), usa a posição inicial como fallback.
        // Isso mantém a compatibilidade com jogos padrão salvos com a versão anterior do 'saveGame'.
        setupInitialPosition();
        resetHistory();
        applyMovesFromPdn(moveLine);
    }

    // Após carregar a posição e aplicar os lances, o histórico está completo.
    // Define o estado atual para o final do jogo carregado.
    m_historyIndex = m_history.size() - 1;
    if (m_historyIndex >= 0) {
        m_coreState = m_history[m_historyIndex];
    }

    // Limpa a UI e recalcula os lances para a posição final
    m_selectedSquare = -1;
    m_isGameOver = false;
    m_movesForSelectedPiece.clear();
    calculatePossibleMoves();
    updateUndoRedoActions();
    updateMoveListWidget();
    update();
    checkGameOver(); // Verifica se a posição carregada é de fim de jogo
}

void CheckerBoard::savePosition()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Salvar Posição"), "",
        tr("FEN (*.fen);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; // O usuário cancelou
    }

    if (!fileName.endsWith(".fen", Qt::CaseInsensitive)) {
        fileName += ".fen";
    }

    QFile file(fileName);
    if (!file.open(QIODeviceBase::WriteOnly)) {
        // TODO: Mostrar uma mensagem de erro para o usuário
        return;
    }

    QTextStream out(&file);
    // Gera o FEN para o estado ATUAL do tabuleiro
    out << generateFen(m_coreState) << Qt::endl;
}

void CheckerBoard::openPosition()
{
    QString fileName = getFileNameWithDelete(this,
        tr("Abrir Posição"), "",
        tr("FEN (*.fen);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; // O usuário cancelou
    }

    QFile file(fileName);
    if (!file.open(QIODeviceBase::ReadOnly)) {
        // TODO: Mostrar uma mensagem de erro para o usuário
        return;
    }

    QTextStream in(&file);
    QString fenLine = in.readLine().trimmed();

    if (loadFromFen(fenLine)) {
        // O FEN foi carregado com sucesso. A função loadFromFen já resetou o histórico.
        // Agora, preparamos a UI para jogar a partir desta posição.
        m_selectedSquare = -1;
        m_isGameOver = false;
        m_movesForSelectedPiece.clear();
        calculatePossibleMoves();
        updateUndoRedoActions();
        updateMoveListWidget();
        update();
        checkGameOver(); // Verifica se a posição carregada é de fim de jogo
    } // TODO: Adicionar mensagem de erro se o FEN for inválido.
}

void CheckerBoard::openBatch()
{
    QString fileName = getFileNameWithDelete(this,
        tr("Abrir Lote de Posições"), "",
        tr("Portable Draughts Notation (*.pdn);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; // O usuário cancelou
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Erro de Leitura"), tr("Não foi possível abrir o arquivo %1.").arg(fileName));
        return;
    }

    QTextStream in(&file);
    QStringList fenList;

    while (!in.atEnd()) {
        QString line = in.readLine();
        int bracketPos = line.indexOf('[');
        if (bracketPos != -1) {
            QString fenString = line.mid(bracketPos).trimmed();
            if (fenString.startsWith("[FEN")) {
                fenList.append(fenString);
            }
        }
    }

    if (fenList.isEmpty()) {
        QMessageBox::information(this, tr("Nenhuma Posição Encontrada"), tr("Nenhuma posição FEN foi encontrada no arquivo selecionado."));
        return;
    }

    bool ok;
    QString selectedFen = QInputDialog::getItem(this, tr("Selecionar Posição FEN"),
                                                tr("Posições encontradas:"), fenList, 0, false, &ok);

    if (ok && !selectedFen.isEmpty()) {
        if (loadFromFen(selectedFen)) {
            m_selectedSquare = -1;
            m_isGameOver = false;
            m_movesForSelectedPiece.clear();
            calculatePossibleMoves();
            updateUndoRedoActions();
            updateMoveListWidget();
            update();
            checkGameOver();
        } else {
            QMessageBox::warning(this, tr("Erro de Formato"), tr("A posição FEN selecionada é inválida."));
        }
    }
}

bool CheckerBoard::loadFromFen(const QString& fen)
{
    // Exemplo: [FEN "W:Wb4,a1:Bh8,a7,Kg3"]
    QRegularExpression re("\\[FEN \"([WB]):([WB])(.*):([WB])(.*)\"\\]");
    QRegularExpressionMatch match = re.match(fen);

    if (!match.hasMatch()) {
        return false;
    }

    // Começa com um estado limpo
    m_coreState.limpar_tabuleiro();
    m_isSetupMode = false;

    // 1. Define o turno
    QString turnStr = match.captured(1);
    DamasCore::GamePlayer turn = (turnStr == "W") ? DamasCore::GamePlayer::BRANCAS : DamasCore::GamePlayer::PRETOS;
    m_coreState.definir_turno_atual(turn);

    // Função auxiliar para analisar e posicionar as peças
    auto parseAndSetPieces = [&](const QString& colorChar, const QString& pieceListStr) {
        DamasCore::GamePlayer player = (colorChar == "W") ? DamasCore::GamePlayer::BRANCAS : DamasCore::GamePlayer::PRETOS;
        QStringList pieces = pieceListStr.split(',', Qt::SkipEmptyParts);
        for (const QString& pieceStr : pieces) {
            bool isKing = false;
            QString posStr = pieceStr;
            if (posStr.startsWith('K')) {
                isKing = true;
                posStr = posStr.mid(1);
            }

            if (posStr.length() == 2) {
                int col = posStr[0].toLatin1() - 'a';
                int row = '8' - posStr[1].toLatin1();
                if (col >= 0 && col < 8 && row >= 0 && row < 8) {
                    int square = row * 8 + col;
                    m_coreState.setPiece(square, isKing, player);
                }
            }
        }
    };

    // 2. Analisa as peças brancas e pretas
    parseAndSetPieces(match.captured(2), match.captured(3)); // Brancas
    parseAndSetPieces(match.captured(4), match.captured(5)); // Pretas

    // Finaliza o estado e reseta o histórico com a nova posição
    m_coreState.recalcular_hash_completo();
    resetHistory(); // Coloca o estado carregado como o primeiro no histórico
    return true;
}

void CheckerBoard::enterSetupMode()
{
    m_isGameOver = false; // Reseta o estado de fim de jogo para permitir a montagem
    m_isSetupMode = true;
    m_selectedSquare = -1;
    m_movesForSelectedPiece.clear();
    m_allPossibleMoves.clear();
    m_setupTool = SetupTool::ERASER; // Começa com a borracha selecionada
    m_isClearButtonInClearState = true; // O botão começa como "Limpar Tabuleiro", pois a posição atual é mantida.

    updateMoveListWidget();

    emit setupModeChanged(true);

    update();
}

void CheckerBoard::setScale(double scale)
{
    updateDimensions(scale);
    initializePaletteRects();
    updateGeometry(); // Informa ao sistema de layout que o sizeHint mudou

    // Pede para a janela pai (QMainWindow) se redimensionar para o novo tamanho
    if (parentWidget()) {
        parentWidget()->setFixedSize(parentWidget()->sizeHint());
    }

    update(); // Força o redesenho com as novas dimensões
}

void CheckerBoard::rotateBoard()
{
    m_isBoardRotated = !m_isBoardRotated;
    update(); // Força o redesenho com a nova orientação
}

void CheckerBoard::undoMove()
{
    if (m_historyIndex > 0) {
        m_isGameOver = false; // Ao voltar um lance, o jogo não está mais terminado
        m_historyIndex--;
        m_coreState = m_history[m_historyIndex];

        m_selectedSquare = -1;
        m_movesForSelectedPiece.clear();
        calculatePossibleMoves();
        updateUndoRedoActions();
        updateMoveListWidget();
        update();
    }
}

void CheckerBoard::redoMove()
{
    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_isGameOver = false; // Assume que não está terminado, checkGameOver irá verificar
        m_historyIndex++;
        m_coreState = m_history[m_historyIndex];

        m_selectedSquare = -1;
        m_movesForSelectedPiece.clear();
        calculatePossibleMoves();
        updateUndoRedoActions();
        updateMoveListWidget();
        update();
        checkGameOver(); // Verifica se o lance refeito termina o jogo
    }
}

void CheckerBoard::setUndoRedoActions(QAction* undo, QAction* redo)
{
    m_undoAction = undo;
    m_redoAction = redo;
    updateUndoRedoActions();
}

void CheckerBoard::setGameMode(CheckerBoard::GameMode mode)
{
    // Caso especial: se estivermos em modo análise e o usuário selecionar Hum vs Hum,
    // interpretamos como um comando para parar a análise e jogar o melhor lance encontrado.
    if (m_gameMode == GameMode::ANALYSIS && mode == GameMode::HUMAN_VS_HUMAN) {
        stopAnalysisAndPlayBestMove();
        return;
    }

    // Para qualquer outra mudança de modo, se uma análise estiver rodando, pare-a sem jogar.
    if (m_aiMoveWatcher.isRunning()) {
        m_playMoveOnAnalysisFinish = false; // Garante que o lance não será jogado
        DamasAI::interrupt_search();
        m_analysisUpdateTimer->stop();
        m_isAiThinking = false;
        unsetCursor();
    }

    m_gameMode = mode;
    m_selectedSquare = -1;
    m_movesForSelectedPiece.clear();

    if (m_gameMode == GameMode::ANALYSIS) {
        startInfiniteAnalysis();
    } else {
        // Para outros modos, verifica se a IA deve jogar imediatamente.
        triggerAiMove();
    }

    // Redesenha para limpar qualquer destaque de seleção anterior
    update();
}

void CheckerBoard::setAiLevel(int level)
{
    // Passa o nível para o módulo da IA
    DamasAI::definir_nivel(level);
}

void CheckerBoard::correctAiMove()
{
    // NOVO FLUXO DE CORREÇÃO (mais intuitivo):
    // 1. A IA (ou oponente) faz um lance ruim.
    // 2. O usuário, em sua vez, aperta Ctrl+L para INICIAR a correção.
    // 3. O sistema desfaz o lance e pede para o usuário jogar o lance correto no lugar do oponente.

    // Garante que não estamos no meio de outra correção.
    if (m_isCorrectingMove) return;

    // Verifica se há um lance para ser corrigido.
    if (m_historyIndex < 1) {
        QMessageBox::warning(this, "Correção Inválida", "Não há nenhum lance no histórico para ser corrigido.");
        return;
    }

    // Verifica se é a vez de um humano jogar (o que implica que a IA ou outro humano acabou de jogar).
    bool isHumanTurn = false;
    const auto currentPlayer = m_coreState.obter_turno_atual();
    if ((m_gameMode == GameMode::HUMAN_VS_AI && currentPlayer == DamasCore::GamePlayer::BRANCAS) ||
        (m_gameMode == GameMode::AI_VS_HUMAN && currentPlayer == DamasCore::GamePlayer::PRETOS) ||
        (m_gameMode == GameMode::HUMAN_VS_HUMAN)) { // Permite corrigir em modo Hum vs Hum para análise
        isHumanTurn = true;
    }

    if (!isHumanTurn || m_isAiThinking) {
        QMessageBox::warning(this, "Aguarde", "A função de correção só pode ser usada no seu turno, imediatamente após um lance do oponente.");
        return;
    }

    // Inicia o modo de correção
    m_isCorrectingMove = true;
    m_correctionStateBad = m_coreState; // Salva o estado RUIM (o resultado do lance do oponente)
    undoMove();

    QMessageBox::information(this, "Modo de Correção", "O último lance foi desfeito.\n\nAgora, jogue no tabuleiro o lance que você acha que deveria ter sido feito.");
}

void CheckerBoard::addCurrentMoveToBook()
{
    // 1. Verifica se há um lance para adicionar.
    // O lance a ser adicionado é o último que foi jogado.
    if (m_historyIndex < 1) {
        QMessageBox::warning(this, "Ação Inválida", "Nenhum lance foi jogado ainda para ser adicionado ao livro.");
        return;
    }

    // 2. O livro de aberturas só cobre os 10 primeiros lances.
    // O estado que vai para o livro é o ANTES do lance, então o índice do histórico deve ser <= 10.
    if (m_historyIndex > 10) {
        QMessageBox::information(this, "Fora do Livro", "O livro de aberturas cobre apenas os 10 primeiros lances do jogo (5 para cada cor).");
        return;
    }

    // 3. Pega o estado ANTES do último lance e o último lance em si.
    const DamasCore::BoardState& state_before_move = m_history[m_historyIndex - 1];
    const DamasCore::GameMove& last_move = m_moveHistory.back();

    // 4. Pede ao usuário o peso para o lance.
    bool ok;
    int weight = QInputDialog::getInt(this, "Adicionar ao Livro",
                                      "Digite o peso para este lance (1-100).\nUm peso maior o torna mais provável de ser jogado.\nUse 0 para remover o lance do livro.",
                                      50, 0, 100, 1, &ok);

    if (ok) {
        DamasAI::adicionar_lance_ao_livro(state_before_move, last_move, weight);
        QMessageBox::information(this, "Sucesso", "O lance foi adicionado/atualizado no livro de aberturas!");
    }
}

void CheckerBoard::updateDimensions(double scale)
{
    m_squareSize = static_cast<int>(BaseSquareSize * scale);
    m_marginSize = static_cast<int>(BaseMarginSize * scale);
}

void CheckerBoard::setupInitialPosition()
{
    // Usa o motor do jogo para configurar a posição inicial
    m_coreState.configurar_posicao_inicial();
}

void CheckerBoard::calculatePossibleMoves()
{
    m_allPossibleMoves.clear();
    m_movesForSelectedPiece.clear();

    // Regra da captura obrigatória: primeiro, procuramos por capturas.
    m_allPossibleMoves = DamasCore::gerar_todas_capturas_maximais(m_coreState);

    // Se não houver capturas, geramos os movimentos simples.
    if (m_allPossibleMoves.empty()) {
        const DamasCore::GamePlayer currentPlayer = m_coreState.obter_turno_atual();
        DamasCore::BoardBitboard my_pieces;

        // Peões
        my_pieces = (currentPlayer == DamasCore::GamePlayer::BRANCAS) ? m_coreState.obter_brancas_peoes() : m_coreState.obter_pretas_peoes();
        while (my_pieces != 0) {
            int from_sq = __builtin_ctzll(my_pieces);
            DamasCore::BoardBitboard destinations = DamasCore::gerar_movimentos_simples_peao(m_coreState, from_sq);
            while (destinations != 0) {
                int to_sq = __builtin_ctzll(destinations);
                m_allPossibleMoves.push_back({from_sq, to_sq, 0});
                destinations &= destinations - 1;
            }
            my_pieces &= my_pieces - 1;
        }

        // Damas
        my_pieces = (currentPlayer == DamasCore::GamePlayer::BRANCAS) ? m_coreState.obter_brancas_damas() : m_coreState.obter_pretas_damas();
        while (my_pieces != 0) {
            int from_sq = __builtin_ctzll(my_pieces);
            DamasCore::BoardBitboard destinations = DamasCore::gerar_movimentos_simples_dama(m_coreState, from_sq);
            while (destinations != 0) {
                int to_sq = __builtin_ctzll(destinations);
                m_allPossibleMoves.push_back({from_sq, to_sq, 0});
                destinations &= destinations - 1;
            }
            my_pieces &= my_pieces - 1;
        }
    }
}

void CheckerBoard::updateMovesForSelection(int square)
{
    m_movesForSelectedPiece.clear();
    if (square < 0 || square > 63) return;

    for (const auto& move : m_allPossibleMoves) {
        if (move.casa_origem == square) {
            m_movesForSelectedPiece.push_back(move);
        }
    }
}

void CheckerBoard::executeMove(const DamasCore::GameMove& move, bool is_ai_move)
{
    // Se estivermos finalizando um processo de correção
    if (m_isCorrectingMove) {
        // O estado atual (m_coreState) é o de ANTES do lance ruim.
        // O 'move' é o lance bom que o usuário acabou de jogar no lugar do oponente.
        DamasCore::BoardState estado_bom = m_coreState;
        if (move.mascaras_pecas_capturadas == 0) {
            estado_bom.aplicar_mov_simples(move.casa_origem, move.casa_destino);
        } else {
            estado_bom.aplicar_movimento_completo_captura(move);
        }
        estado_bom.alternar_turno();

        // Compara o estado bom (resultado do lance do usuário) com o estado ruim (salvo anteriormente)
        DamasAI::aprender_com_correcao(estado_bom, m_correctionStateBad);

        QMessageBox::information(this, "Aprendizado Aplicado", "A correção foi aprendida com sucesso.\nO jogo continuará a partir do lance que você ensinou.");
        m_isCorrectingMove = false;
        // O lance foi feito por um humano, mesmo que no lugar da IA, então is_ai_move é false.
        is_ai_move = false;
    }

    // Aplica o movimento no motor do jogo
    if (move.mascaras_pecas_capturadas == 0) {
        m_coreState.aplicar_mov_simples(move.casa_origem, move.casa_destino);
    } else {
        m_coreState.aplicar_movimento_completo_captura(move);
    }

    // Passa o turno
    m_coreState.alternar_turno();

    // Atualiza o histórico de jogadas
    // Remove qualquer histórico futuro se estivermos "voltando no tempo"
    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_history.resize(m_historyIndex + 1);
        m_moveHistory.resize(m_historyIndex);
    }
    m_history.push_back(m_coreState);
    m_moveHistory.push_back(move);
    m_historyIndex++;
    updateUndoRedoActions();
    updateMoveListWidget();

    // Reseta o estado da UI
    m_selectedSquare = -1;
    m_allPossibleMoves.clear();
    m_movesForSelectedPiece.clear();

    // Calcula os movimentos para o próximo jogador
    {
        // Toca o som de movimento usando um comando de sistema (`aplay`).
        // Isso é mais robusto em diferentes distribuições Linux do que usar o QtMultimedia.
        QString soundFileName = "move.wav";
        QStringList searchPaths;
        // Caminho para quando o app está instalado (ex: /opt/damas-brasileiras/)
        searchPaths << QCoreApplication::applicationDirPath() + "/audio/" + soundFileName;
        // Caminho para quando executado do diretório de build (ex: build/)
        searchPaths << QCoreApplication::applicationDirPath() + "/../audio/" + soundFileName;

        for (const QString &path : searchPaths) {
            if (QFile::exists(path)) {
                // Usa startDetached para não bloquear a GUI. O "-q" suprime a saída do aplay.
                QProcess::startDetached("aplay", QStringList() << "-q" << path);
                break; // Para após encontrar o arquivo
            }
        }
        // Nota: Se o som não tocar, verifique se o pacote 'alsa-utils' está instalado.
        // Ele foi adicionado como dependência no script create_deb.sh.
    }

    // Solicita um redesenho
    update();

    // As ações que preparam o tabuleiro para o próximo jogador (humano ou IA)
    // são executadas imediatamente para que a UI fique responsiva.
    calculatePossibleMoves();
    update(); // Redesenha para mostrar os destaques de lances possíveis.
    checkGameOver();

    // Se o jogo não acabou, verificamos se o próximo jogador é a IA.
    if (!m_isGameOver) {
        bool isNextTurnAi = false;
        const auto nextPlayer = m_coreState.obter_turno_atual();
        if (m_gameMode == GameMode::AI_VS_AI ||
            (m_gameMode == GameMode::HUMAN_VS_AI && nextPlayer == DamasCore::GamePlayer::PRETOS) ||
            (m_gameMode == GameMode::AI_VS_HUMAN && nextPlayer == DamasCore::GamePlayer::BRANCAS)) {
            isNextTurnAi = true;
        }

        // Se for a vez da IA, acionamos o movimento dela com um delay para um efeito visual mais agradável.
        // Se for a vez de um humano, a função termina aqui e o tabuleiro já está pronto para o clique.
        if (isNextTurnAi) {
            int delay_ms = 700;
            // Se o lance anterior foi da IA e foi um lance de EGTB, o delay é menor.
            if (is_ai_move && m_currentAnalysisInfo.depth == 100) {
                delay_ms = 500;
            }
            QTimer::singleShot(delay_ms, this, &CheckerBoard::triggerAiMove);
        }
    }
}

void CheckerBoard::applyMovesFromPdn(const QString& moveText)
{
    // Exemplo: "1. c3-d4 d6-c5 2. b2-c3 ..." ou "1. ... h6-g5"
    QStringList tokens = moveText.split(' ', Qt::SkipEmptyParts);

    // Função auxiliar para converter notação algébrica para índice de casa
    auto algebraicToSquare = [](const QString& alg) -> int {
        if (alg.length() != 2) return -1;
        int col = alg[0].toLatin1() - 'a';
        int row = '8' - alg[1].toLatin1();
        if (col < 0 || col >= 8 || row < 0 || row >= 8) return -1;
        return row * 8 + col;
    };

    for (const QString& token : tokens) {
        // Pula números de lance como "1." ou "..."
        if (token.endsWith('.') || token == "...") {
            continue;
        }

        // Temos um token de lance como "a3-b4" ou "c3xe5"
        QString separator;
        if (token.contains('x')) {
            separator = "x";
        } else if (token.contains('-')) {
            separator = "-";
        } else {
            continue; // Formato de lance inválido
        }

        QStringList parts = token.split(separator);
        if (parts.size() != 2) continue;

        int from_sq = algebraicToSquare(parts[0]);
        int to_sq = algebraicToSquare(parts[1]);

        if (from_sq == -1 || to_sq == -1) continue;

        // Gera os lances legais para o estado atual para encontrar o lance correspondente
        calculatePossibleMoves();

        bool move_found = false;
        for (const auto& legal_move : m_allPossibleMoves) {
            if (legal_move.casa_origem == from_sq && legal_move.casa_destino == to_sq) {
                // Encontramos o lance. Aplica-o para avançar o estado do jogo.
                if (legal_move.mascaras_pecas_capturadas == 0) {
                    m_coreState.aplicar_mov_simples(legal_move.casa_origem, legal_move.casa_destino);
                } else {
                    m_coreState.aplicar_movimento_completo_captura(legal_move);
                }
                m_coreState.alternar_turno();

                // Adiciona o estado e o lance ao histórico
                m_history.push_back(m_coreState);
                m_moveHistory.push_back(legal_move);

                move_found = true;
                break;
            }
        }
        if (!move_found) break; // Se um lance for ilegal, para de processar
    }
}

QString CheckerBoard::generateFen(const DamasCore::BoardState& state) const
{
    QString fen = "[FEN \"";

    // 1. Turno
    fen += (state.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? "W" : "B";
    fen += ":";

    // 2. Peças
    QStringList white_list;
    QStringList black_list;


    DamasCore::BoardBitboard bb_w_p = state.obter_brancas_peoes();
    DamasCore::BoardBitboard bb_w_k = state.obter_brancas_damas();
    DamasCore::BoardBitboard bb_b_p = state.obter_pretas_peoes();
    DamasCore::BoardBitboard bb_b_k = state.obter_pretas_damas();

    // Itera pelas casas na ordem solicitada (índice 0 a 63),
    // que corresponde a escanear de h8 a a1.
    for (int sq = 0; sq < 64; ++sq) {
        const DamasCore::BoardBitboard mask = 1ULL << sq;
        
        if ((bb_w_p & mask) != 0) {
            white_list.append(squareToAlgebraic(sq));
        } else if ((bb_w_k & mask) != 0) {
            white_list.append("K" + squareToAlgebraic(sq));
        } else if ((bb_b_p & mask) != 0) {
            black_list.append(squareToAlgebraic(sq));
        } else if ((bb_b_k & mask) != 0) {
            black_list.append("K" + squareToAlgebraic(sq));
        }
    }

    // Monta a string final
    fen += "W";
    if (!white_list.isEmpty()) {
        fen += white_list.join(',');
    }

    fen += ":B";
    if (!black_list.isEmpty()) {
        fen += black_list.join(',');
    }

    fen += "\"]";

    return fen;
}

QString CheckerBoard::squareToAlgebraic(int square) const
{
    if (square < 0 || square > 63) return "";
    int row = square / 8;
    int col = square % 8;
    char file = 'a' + col;
    char rank = '8' - row;
    return QString(file) + QString(rank);
}

QString CheckerBoard::moveToAlgebraic(const DamasCore::GameMove& move) const
{
    QString from = squareToAlgebraic(move.casa_origem);
    QString to = squareToAlgebraic(move.casa_destino);
    // Para capturas, o formato é "origem"x"destino"
    QString separator = (move.mascaras_pecas_capturadas == 0) ? "-" : "x";
    return from + separator + to;
}

CheckerBoard::Piece CheckerBoard::getPieceAt(int row, int col) const
{
    // Converte a coordenada visual (linha, coluna) para o índice lógico do bitboard (0-63)
    // O estado do jogo (m_coreState) está sempre na orientação padrão (brancas na base).
    // A GUI pode estar rotacionada.
    int logical_row = row;
    int logical_col = col;
    if (m_isBoardRotated) {
        logical_row = 7 - row;
        logical_col = 7 - col;
    }
    const int square = logical_row * 8 + logical_col;
    const DamasCore::BoardBitboard mask = 1ULL << square;

    // Verifica cada bitboard para encontrar a peça
    if ((m_coreState.obter_brancas_peoes() & mask) != 0) return Piece::WHITE_PAWN;
    if ((m_coreState.obter_pretas_peoes() & mask) != 0) return Piece::BLACK_PAWN;
    if ((m_coreState.obter_brancas_damas() & mask) != 0) return Piece::WHITE_KING;
    if ((m_coreState.obter_pretas_damas() & mask) != 0) return Piece::BLACK_KING;

    // Se não encontrou em nenhum bitboard, a casa está vazia
    return Piece::NONE;
}

void CheckerBoard::resetHistory()
{
    m_history.clear();
    m_moveHistory.clear();
    m_history.push_back(m_coreState);
    m_historyIndex = 0;
    updateUndoRedoActions();
}

void CheckerBoard::initializePaletteRects()
{
    const int boardPixelSize = BoardSize * m_squareSize;
    const int panelPixelWidth = PanelWidthInSquares * m_squareSize;
    const int panelX = m_marginSize + boardPixelSize + m_marginSize;

    m_paletteRect = QRect(panelX, m_marginSize, panelPixelWidth, boardPixelSize);

    const int toolSize = m_squareSize - 10;
    const int toolMarginX = (panelPixelWidth - toolSize) / 2;
    int currentY = m_marginSize + 10;

    m_eraserToolRect = QRect(panelX + toolMarginX, currentY, toolSize, toolSize);
    currentY += toolSize + 5;
    m_whitePawnToolRect = QRect(panelX + toolMarginX, currentY, toolSize, toolSize);
    currentY += toolSize + 5;
    m_blackPawnToolRect = QRect(panelX + toolMarginX, currentY, toolSize, toolSize);
    currentY += toolSize + 5;
    m_whiteKingToolRect = QRect(panelX + toolMarginX, currentY, toolSize, toolSize);
    currentY += toolSize + 5;
    m_blackKingToolRect = QRect(panelX + toolMarginX, currentY, toolSize, toolSize);

    currentY += toolSize + 15; // Espaço antes dos botões
    m_clearBoardButtonRect = QRect(panelX + 10, currentY, panelPixelWidth - 20, 30);
    currentY += 30 + 5;
    m_playerTurnButtonRect = QRect(panelX + 10, currentY, panelPixelWidth - 20, 30);

    // Botão "Pronto" na parte inferior
    m_doneButtonRect = QRect(panelX + 10, m_marginSize + boardPixelSize - 40, panelPixelWidth - 20, 30);
}

void CheckerBoard::updateUndoRedoActions()
{
    if (m_undoAction) {
        m_undoAction->setEnabled(m_historyIndex > 0);
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_historyIndex < static_cast<int>(m_history.size()) - 1);
    }
}

void CheckerBoard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event); // Informa ao compilador que não usamos o parâmetro 'event'

    QPainter painter(this);
    // Habilita o antialiasing para ter bordas e textos mais suaves
    painter.setRenderHint(QPainter::Antialiasing);

    // --- 1. Desenhar as casas do tabuleiro ---
    for (int row = 0; row < BoardSize; ++row) {
        for (int col = 0; col < BoardSize; ++col) {
            // Alterna a cor com base na soma da linha e coluna
            if ((row + col) % 2 == 0) {
                painter.setBrush(LightSquareColor);
            } else {
                painter.setBrush(DarkSquareColor);
            }

            // Desenha o retângulo da casa. A margem é adicionada para centralizar o tabuleiro.
            painter.drawRect(m_marginSize + col * m_squareSize,
                             m_marginSize + row * m_squareSize,
                             m_squareSize, m_squareSize);
        }
    }

    // --- 2. Desenhar as peças ---
    painter.setRenderHint(QPainter::Antialiasing, true); // Garante que as peças sejam redondas
    for (int row = 0; row < BoardSize; ++row) {
        for (int col = 0; col < BoardSize; ++col) {
            const Piece piece = getPieceAt(row, col);
            if (piece != Piece::NONE) {
                // Define a margem da peça dentro da casa
                const int pieceMargin = 5;
                QRect pieceRect(
                    m_marginSize + col * m_squareSize + pieceMargin,
                    m_marginSize + row * m_squareSize + pieceMargin,
                    m_squareSize - 2 * pieceMargin,
                    m_squareSize - 2 * pieceMargin
                );
                
                // Define a cor da peça e da borda
                if (piece == Piece::WHITE_PAWN || piece == Piece::WHITE_KING) {
                    painter.setBrush(Qt::white);
                    painter.setPen(QPen(Qt::black, 2)); // Borda preta
                } else { // BLACK_PAWN ou BLACK_KING
                    painter.setBrush(QColor("#111111")); // Um preto não tão absoluto
                    painter.setPen(QPen(Qt::darkGray, 2)); // Borda cinza
                }
                painter.drawEllipse(pieceRect);

                // Desenha uma "coroa" (círculo) para as damas (reis)
                if (piece == Piece::WHITE_KING || piece == Piece::BLACK_KING) {
                    const int kingMargin = 15; // Margem maior para o círculo interno
                    QRect kingMarkRect(
                        m_marginSize + col * m_squareSize + kingMargin,
                        m_marginSize + row * m_squareSize + kingMargin,
                        m_squareSize - 2 * kingMargin,
                        m_squareSize - 2 * kingMargin
                    );
                    painter.drawEllipse(kingMarkRect);
                }
            }
        }
    }

    // --- 3. Desenhar destaques de seleção e movimentos ---
    if (m_selectedSquare != -1) {
        // Destaque para a casa selecionada
        // m_selectedSquare é um índice lógico, precisamos convertê-lo para uma posição visual.
        int logical_row = m_selectedSquare / 8;
        int logical_col = m_selectedSquare % 8;

        int visual_row = logical_row;
        int visual_col = logical_col;
        if (m_isBoardRotated) {
            visual_row = 7 - logical_row;
            visual_col = 7 - logical_col;
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 0, 200), 4)); // Amarelo translúcido
        painter.drawRect(m_marginSize + visual_col * m_squareSize + 2, m_marginSize + visual_row * m_squareSize + 2, m_squareSize - 4, m_squareSize - 4);

        // Destaque para os movimentos possíveis
        painter.setBrush(QColor(0, 0, 0, 80)); // Círculo cinza translúcido
        painter.setPen(Qt::NoPen);
        for (const auto& move : m_movesForSelectedPiece) {
            // move.casa_destino também é um índice lógico.
            int dest_logical_row = move.casa_destino / 8;
            int dest_logical_col = move.casa_destino % 8;

            int dest_visual_row = dest_logical_row;
            int dest_visual_col = dest_logical_col;
            if (m_isBoardRotated) {
                dest_visual_row = 7 - dest_logical_row;
                dest_visual_col = 7 - dest_logical_col;
            }

            const int highlightMargin = 22;
            painter.drawEllipse(
                m_marginSize + dest_visual_col * m_squareSize + highlightMargin,
                m_marginSize + dest_visual_row * m_squareSize + highlightMargin,
                m_squareSize - 2 * highlightMargin,
                m_squareSize - 2 * highlightMargin
            );
        }
    }

    // Variáveis de tamanho para o painel e coordenadas
    const int boardPixelSize = BoardSize * m_squareSize;
    const int panelPixelWidth = PanelWidthInSquares * m_squareSize;

    // --- 4. Desenhar paleta de montagem (se estiver no modo) ---
    if (m_isSetupMode) {
        drawSetupPalette(painter);
        return; // Não desenha o painel normal nem as coordenadas
    }

    // --- 4. Desenhar o painel lateral e de análise ---
    drawAnalysisPanel(painter);

    // --- 5. Desenhar as coordenadas (réguas) ---
    painter.setPen(Qt::black); // Cor do texto
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);

    for (int i = 0; i < BoardSize; ++i) {
        // Coordenadas horizontais (a-h)
        QString letter = QChar('a' + (m_isBoardRotated ? 7 - i : i));
        QRect horizontalRect(m_marginSize + i * m_squareSize, 0, m_squareSize, m_marginSize);
        // Desenha em cima e embaixo
        painter.drawText(horizontalRect, Qt::AlignCenter, letter);
        painter.drawText(horizontalRect.translated(0, m_marginSize + boardPixelSize), Qt::AlignCenter, letter);

        // Coordenadas verticais (8-1)
        QString number = QString::number(m_isBoardRotated ? i + 1 : 8 - i);
        QRect verticalRect(0, m_marginSize + i * m_squareSize, m_marginSize, m_squareSize);
        // Desenha na esquerda e na direita
        painter.drawText(verticalRect, Qt::AlignCenter, number);
        // A régua da direita agora fica entre o tabuleiro e o painel
        painter.drawText(verticalRect.translated(m_marginSize + boardPixelSize, 0), Qt::AlignCenter, number);
    }
}

void CheckerBoard::updateMoveListWidget()
{
    QStringList move_lines;

    if (m_historyIndex > 0) {
        int move_number = 1;
        // O turno do primeiro lance é determinado pelo estado inicial no histórico.
        bool is_white_turn = (m_history[0].obter_turno_atual() == DamasCore::GamePlayer::BRANCAS);

        QString current_line;

        for (int i = 0; i < m_historyIndex; ++i) {
            if (is_white_turn) {
                // Começa uma nova linha para o lance das brancas
                if (!current_line.isEmpty()) {
                    move_lines.append(current_line);
                }
                current_line = QString("%1. %2").arg(move_number).arg(moveToAlgebraic(m_moveHistory[i]));
            } else { // Lance das pretas
                if (i == 0) {
                    // Caso especial: o primeiro lance do jogo é das pretas
                    current_line = QString("%1. ... %2").arg(move_number).arg(moveToAlgebraic(m_moveHistory[i]));
                } else {
                    // Adiciona o lance das pretas à linha atual
                    current_line += " " + moveToAlgebraic(m_moveHistory[i]);
                }
                // Após o lance das pretas, a linha está completa e o número do lance incrementa
                move_lines.append(current_line);
                current_line.clear();
                move_number++;
            }

            // Alterna o turno para o próximo lance no histórico
            is_white_turn = !is_white_turn;
        }

        // Adiciona a última linha se ela não estiver vazia (ex: jogo termina com lance das brancas)
        if (!current_line.isEmpty()) {
            move_lines.append(current_line);
        }
    }

    m_moveListWidget->setPlainText(move_lines.join('\n'));
    // Garante que o último lance esteja visível
    m_moveListWidget->verticalScrollBar()->setValue(m_moveListWidget->verticalScrollBar()->maximum());
}

void CheckerBoard::updateAnalysisInfo()
{
    m_currentAnalysisInfo = DamasAI::get_current_so();
    update(); // Força o redesenho do painel de análise
}

void CheckerBoard::onAiMoveFinished()
{
    // Pega o resultado do watcher (o lance calculado pela IA)
    DamasCore::GameMove aiMove = m_aiMoveWatcher.result();

    // Para o timer de atualização e pega os dados finais da análise
    m_analysisUpdateTimer->stop();
    m_currentAnalysisInfo = DamasAI::get_current_so();

    // Reabilita a interação do usuário e restaura o cursor
    m_isAiThinking = false;
    unsetCursor();

    // Condição para jogar o lance:
    // 1. O usuário explicitamente pediu para jogar (m_playMoveOnAnalysisFinish).
    // 2. A análise estava rodando e encontrou uma solução "perfeita" (depth >= 100).
    bool shouldPlayTheMove = m_playMoveOnAnalysisFinish || (m_gameMode == GameMode::ANALYSIS && m_currentAnalysisInfo.depth >= 100);

    if (shouldPlayTheMove) {
        m_playMoveOnAnalysisFinish = false; // Reseta a flag

        // Muda o modo de jogo para Hum vs Hum e notifica a UI
        m_gameMode = GameMode::HUMAN_VS_HUMAN;
        emit gameModeChanged(GameMode::HUMAN_VS_HUMAN);

        if (aiMove.casa_origem != -1) {
            executeMove(aiMove, true);
        } else {
            // Se nenhum lance foi encontrado, apenas atualiza o estado
            calculatePossibleMoves();
            update();
        }
    }
    // Se for um turno normal da IA (não uma análise interrompida para jogar), executa o lance.
    else if (m_gameMode != GameMode::ANALYSIS && m_gameMode != GameMode::HUMAN_VS_HUMAN) {
        if (aiMove.casa_origem != -1) {
            executeMove(aiMove, true);
        }
    }
    // Se a análise foi apenas interrompida (sem pedir para jogar), apenas atualiza o painel.
    else {
        update();
    }
}

void CheckerBoard::triggerAiMove()
{
    bool isAiTurn = false;
    const auto currentPlayer = m_coreState.obter_turno_atual();

    if (m_gameMode == GameMode::AI_VS_AI) {
        isAiTurn = true;
    } else if (m_gameMode == GameMode::HUMAN_VS_AI && currentPlayer == DamasCore::GamePlayer::PRETOS) {
        isAiTurn = true;
    } else if (m_gameMode == GameMode::AI_VS_HUMAN && currentPlayer == DamasCore::GamePlayer::BRANCAS) {
        isAiTurn = true;
    }

    // Verifica também se uma busca já não está em andamento
    if (isAiTurn && m_allPossibleMoves.size() > 0 && !m_aiMoveWatcher.isRunning()) {
        // Limpa a análise anterior e inicia o timer para atualizações em tempo real
        m_currentAnalysisInfo = {};
        update();
        m_analysisUpdateTimer->start();

        // Bloqueia a interação do usuário e muda o cursor para indicar que a IA está pensando
        m_isAiThinking = true;
        setCursor(Qt::WaitCursor);
        // Captura o estado atual do tabuleiro para passar para o thread de forma segura
        DamasCore::BoardState currentState = m_coreState;
        int currentPly = m_historyIndex;
        // Executa a busca da IA em um thread separado para não bloquear a GUI, passando o número de lances
        QFuture<DamasCore::GameMove> future = QtConcurrent::run(DamasAI::encontrar_melhor_lance, currentState, currentPly);
        m_aiMoveWatcher.setFuture(future);
    }
}

void CheckerBoard::startInfiniteAnalysis()
{
    if (m_allPossibleMoves.empty()) {
        calculatePossibleMoves();
    }

    if (m_allPossibleMoves.size() > 0 && !m_aiMoveWatcher.isRunning()) {
        m_currentAnalysisInfo = {};
        update();
        m_analysisUpdateTimer->start();
        m_isAiThinking = true;
        setCursor(Qt::WaitCursor);
        DamasCore::BoardState currentState = m_coreState;
        int currentPly = m_historyIndex;
        QFuture<DamasCore::GameMove> future = QtConcurrent::run(DamasAI::encontrar_melhor_lance_infinito, currentState, currentPly);
        m_aiMoveWatcher.setFuture(future);
    }
}

void CheckerBoard::stopAnalysisAndPlayBestMove()
{
    if (m_aiMoveWatcher.isRunning()) {
        m_playMoveOnAnalysisFinish = true; // Sinaliza para onAiMoveFinished que o lance deve ser jogado
        DamasAI::interrupt_search();   // Interrompe a busca, o que acionará onAiMoveFinished
    }
}

void CheckerBoard::drawAnalysisPanel(QPainter& painter)
{
    const int boardPixelSize = BoardSize * m_squareSize;
    const int panelPixelWidth = PanelWidthInSquares * m_squareSize;
    const QRect panelRect(
        m_marginSize + boardPixelSize + m_marginSize,
        m_marginSize,
        panelPixelWidth,
        boardPixelSize
    );

    // Fundo do painel
    painter.setBrush(PanelColor);
    painter.setPen(Qt::NoPen);
    painter.drawRect(panelRect);

    // Painel de Análise (metade superior)
    const QRect analysisPanelRect(panelRect.x(), panelRect.y(), panelRect.width(), panelRect.height() / 2);

    // Título "Análise" - Centralizado
    painter.setPen(Qt::black);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRect titleRect = analysisPanelRect.adjusted(0, 5, 0, 0);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop, "Análise");

    // Informações da Análise
    QFont infoFont = painter.font();
    infoFont.setBold(false);
    painter.setFont(infoFont);

    // Cria uma fonte menor para a "Bestline" para evitar que o texto invada outras áreas
    QFont pvFont = infoFont;
    int newSize = pvFont.pointSize() - 3;
    if (newSize > 5) pvFont.setPointSize(newSize); // Evita que a fonte fique ilegível

    const int lineHeight = painter.fontMetrics().height() + 3;
    int currentY = analysisPanelRect.y() + 35; // Começa do topo, abaixo do título "Análise"
    const int leftMargin = analysisPanelRect.x() + 10;

    // 1. Desenha o título "Bestline" centralizado
    painter.setPen(Qt::black);
    painter.setFont(titleFont); // Usa a fonte em negrito para o título
    painter.drawText(QRect(analysisPanelRect.x(), currentY, analysisPanelRect.width(), lineHeight), Qt::AlignHCenter | Qt::AlignVCenter, "Bestline");
    currentY += lineHeight;

    // Usa a fonte menor para os lances da PV
    painter.setFont(pvFont);
    const int pvLineHeight = painter.fontMetrics().height() + 2; // Um espaçamento um pouco menor

    // Define a cor azul para os valores da "Bestline"
    painter.setPen(Qt::blue);

    // 2. Formata e desenha os lances da PV (melhor linha) com numeração
    int linesDrawn = 0;
    if (!m_currentAnalysisInfo.pv.empty()) {
        QStringList moves_algebraic;
        for(const auto& move : m_currentAnalysisInfo.pv) {
            moves_algebraic.append(moveToAlgebraic(move));
        }

        int move_number = 1;
        bool is_white_turn_for_numbering = (m_coreState.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS);
        int move_idx = 0;

        while(move_idx < moves_algebraic.size() && linesDrawn < 4) {
            QStringList line_parts;
            int moves_on_this_line = 0;

            while(move_idx < moves_algebraic.size() && moves_on_this_line < 4) {
                if (is_white_turn_for_numbering) {
                    line_parts.append(QString("%1.").arg(move_number));
                    line_parts.append(moves_algebraic[move_idx]);
                } else { // Black's turn
                    // Adiciona "..." apenas se for o primeiro lance da PV
                    if (move_idx == 0) {
                        line_parts.append(QString("%1.").arg(move_number));
                        line_parts.append("...");
                    }
                    line_parts.append(moves_algebraic[move_idx]);
                }

                move_idx++;
                moves_on_this_line++;
                
                // Alterna o turno e incrementa o número do lance após os lances das pretas
                if (!is_white_turn_for_numbering) {
                    move_number++;
                }
                is_white_turn_for_numbering = !is_white_turn_for_numbering;
            }

            QString line = line_parts.join(" ");
            painter.drawText(QRect(leftMargin, currentY, analysisPanelRect.width() - 20, pvLineHeight), Qt::AlignLeft | Qt::AlignVCenter, line);
            currentY += pvLineHeight;
            linesDrawn++;
        }
    }
    if (linesDrawn > 0) {
        currentY += 5; // Adiciona um espaço extra após a lista de lances
    } else {
        // Se não houver lances, adiciona um espaço para não colar nas outras infos
        currentY += pvLineHeight;
    }

    // Restaura a fonte e a cor originais para o resto das informações
    painter.setFont(infoFont);
    painter.setPen(Qt::black);

    // 3. Desenha o resto das informações (Score, Prof, etc.)
    // Calcula a largura da label mais longa para alinhar os valores corretamente
    QFontMetrics fm(infoFont);
    int maxLabelWidth = 0;
    maxLabelWidth = std::max(maxLabelWidth, fm.horizontalAdvance("Score: "));
    maxLabelWidth = std::max(maxLabelWidth, fm.horizontalAdvance("Prof: "));
    maxLabelWidth = std::max(maxLabelWidth, fm.horizontalAdvance("Pos: "));
    maxLabelWidth = std::max(maxLabelWidth, fm.horizontalAdvance("Tem: "));

    // Reposiciona currentY na base do painel para as informações fixas, evitando o "efeito sanfona"
    currentY = analysisPanelRect.bottom() - (4 * lineHeight) - 5;

    const int valueX = leftMargin + maxLabelWidth + 5; // Adiciona 5 pixels de espaçamento
    const int valueWidth = analysisPanelRect.right() - valueX - 5;

    auto drawInfoLine = [&](const QString& label, const QString& value) {
        // Desenha a label e o valor, ambos centralizados verticalmente na mesma linha.
        painter.setPen(Qt::black); // Garante que a label seja preta
        painter.drawText(QRect(leftMargin, currentY, maxLabelWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter, label);

        painter.setPen(Qt::red); // Define a cor vermelha para o valor
        QRect valueRect(valueX, currentY, valueWidth, lineHeight);
        painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, value);
        currentY += lineHeight;
    };

    // Formata os dados para exibição
    QString score_string = "0.00";
    if (m_isAiThinking || m_currentAnalysisInfo.depth > 0) {
        score_string = QString::number(m_currentAnalysisInfo.score / 100.0, 'f', 2);
        if (m_currentAnalysisInfo.score > DamasAI::SCORE_MATE - 100) {
            score_string = QString("Mate %1").arg((DamasAI::SCORE_MATE - m_currentAnalysisInfo.score + 1) / 2);
        } else if (m_currentAnalysisInfo.score < -DamasAI::SCORE_MATE + 100) {
            score_string = QString("Mate %1").arg((DamasAI::SCORE_MATE + m_currentAnalysisInfo.score + 1) / 2);
        }
    }

    // Formata a string de tempo
    QString time_string;
    double total_seconds = m_currentAnalysisInfo.time_spent;
    if (total_seconds < 60.0) {
        time_string = QString::number(total_seconds, 'f', 1) + "s";
    } else {
        int minutes = static_cast<int>(total_seconds) / 60;
        int seconds = static_cast<int>(total_seconds) % 60;
        time_string = QString("%1m%2s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    }

    // Desenha as informações restantes
    drawInfoLine("Score: ", score_string);
    drawInfoLine("Prof: ", (m_isAiThinking || m_currentAnalysisInfo.depth > 0) ? QString::number(m_currentAnalysisInfo.depth) : "0");
    drawInfoLine("Pos: ", (m_isAiThinking || m_currentAnalysisInfo.node > 0) ? QString::fromStdString(DamasAI::formatar_numero_com_pontos(m_currentAnalysisInfo.node)) : "0");
    drawInfoLine("Tem: ", time_string);

    // Restaura a cor da caneta para o padrão para não afetar outros desenhos
    painter.setPen(Qt::black);

    // Painel de Lances (metade inferior)
    const QRect movesPanelRect(panelRect.x(), panelRect.y() + panelRect.height() / 2, panelRect.width(), panelRect.height() / 2);
    painter.setFont(titleFont);
    painter.drawText(movesPanelRect.adjusted(10, 5, -10, 0), Qt::AlignLeft | Qt::AlignTop, "lances jogados");
    painter.setPen(QPen(Qt::darkGray, 1));
    painter.drawLine(analysisPanelRect.bottomLeft(), analysisPanelRect.bottomRight());
}

void CheckerBoard::drawSetupPalette(QPainter& painter)
{
    // Fundo da paleta
    painter.setBrush(PanelColor);
    painter.setPen(Qt::NoPen);
    painter.drawRect(m_paletteRect);

    // Função auxiliar para desenhar uma peça na paleta
    auto drawToolPiece = [&](const QRect& rect, Piece piece, SetupTool tool) {
        // Destaque se selecionado
        if (m_setupTool == tool) {
            painter.setBrush(DarkSquareColor);
            painter.drawRect(rect);
        }
        
        const int pieceMargin = 4;
        QRect pieceRect = rect.adjusted(pieceMargin, pieceMargin, -pieceMargin, -pieceMargin);

        if (piece == Piece::WHITE_PAWN || piece == Piece::WHITE_KING) {
            painter.setBrush(Qt::white); painter.setPen(QPen(Qt::black, 2));
        } else {
            painter.setBrush(QColor("#111111")); painter.setPen(QPen(Qt::darkGray, 2));
        }
        painter.drawEllipse(pieceRect);

        if (piece == Piece::WHITE_KING || piece == Piece::BLACK_KING) {
            const int kingMargin = 12;
            painter.drawEllipse(rect.adjusted(kingMargin, kingMargin, -kingMargin, -kingMargin));
        }
    };

    // Desenhar Borracha
    if (m_setupTool == SetupTool::ERASER) {
        painter.setBrush(DarkSquareColor);
        painter.drawRect(m_eraserToolRect);
    }
    painter.setPen(QPen(Qt::red, 4));
    painter.drawLine(m_eraserToolRect.topLeft(), m_eraserToolRect.bottomRight());
    painter.drawLine(m_eraserToolRect.topRight(), m_eraserToolRect.bottomLeft());

    // Desenhar as peças da paleta
    drawToolPiece(m_whitePawnToolRect, Piece::WHITE_PAWN, SetupTool::WHITE_PAWN);
    drawToolPiece(m_blackPawnToolRect, Piece::BLACK_PAWN, SetupTool::BLACK_PAWN);
    drawToolPiece(m_whiteKingToolRect, Piece::WHITE_KING, SetupTool::WHITE_KING);
    drawToolPiece(m_blackKingToolRect, Piece::BLACK_KING, SetupTool::BLACK_KING);

    // Função auxiliar para desenhar botões
    auto drawButton = [&](const QRect& rect, const QString& text) {
        painter.setBrush(QColor("#C0C0C0"));
        painter.setPen(Qt::black);
        painter.drawRoundedRect(rect, 5, 5);
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    // Desenhar botões
    QString clearButtonText = m_isClearButtonInClearState ? "Limpar Tabuleiro" : "Posição Inicial";
    drawButton(m_clearBoardButtonRect, clearButtonText);
    QString turnText = (m_coreState.obter_turno_atual() == DamasCore::GamePlayer::BRANCAS) ? "Vez: Brancas" : "Vez: Pretas";
    drawButton(m_playerTurnButtonRect, turnText);
    drawButton(m_doneButtonRect, "Pronto");
}

void CheckerBoard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int boardPixelSize = BoardSize * m_squareSize;
    const int panelPixelWidth = PanelWidthInSquares * m_squareSize;
    const int panelX = m_marginSize + boardPixelSize + m_marginSize;

    // Calcula a área para a lista de lances
    QRect movesPanelRect(panelX, m_marginSize + boardPixelSize / 2, panelPixelWidth, boardPixelSize / 2);

    // Precisamos de um QFontMetrics para saber a altura do título
    QFontMetrics fm(font());
    const int title_height = fm.height();

    // Ajusta o retângulo para ficar abaixo do título "lances jogados"
    QRect moveListRect = movesPanelRect.adjusted(10, 5 + title_height + 5, -10, -10);

    m_moveListWidget->setGeometry(moveListRect);
}

void CheckerBoard::checkGameOver()
{
    if (m_isGameOver) return;

    // --- Checagem de Vitória/Derrota ---
    // Se o jogador atual não tem lances, ele perde.
    // Isso cobre tanto a falta de peças quanto o bloqueio total.
    if (m_allPossibleMoves.empty()) {
        DamasCore::GamePlayer currentPlayer = m_coreState.obter_turno_atual();
        QString winner = (currentPlayer == DamasCore::GamePlayer::BRANCAS) ? "PRETAS" : "BRANCAS";
        QMessageBox::information(this, "Fim de Jogo", QString("%1 VENCEM").arg(winner));
        m_isGameOver = true;
        update(); // Redesenha para limpar destaques de lances
        return; // O jogo acabou, não precisa checar empates
    }

    // --- Checagem de Empate ---

    // 1. Regra dos 40 lances (20 de cada lado) sem captura ou movimento de peão
    if (m_coreState.obter_relogio_meio_movimento() >= 40) {
        QMessageBox::information(this, "Fim de Jogo", "EMPATE");
        m_isGameOver = true;
        m_allPossibleMoves.clear(); // Impede lances futuros
        update();
        return;
    }

    // 2. Repetição de posição 3 vezes
    uint64_t currentHash = m_coreState.obter_hash();
    int repetitionCount = 0;
    for (const auto& state : m_history) {
        if (state.obter_hash() == currentHash) {
            repetitionCount++;
        }
    }

    if (repetitionCount >= 3) {
        QMessageBox::information(this, "Fim de Jogo", "EMPATE");
        m_isGameOver = true;
        m_allPossibleMoves.clear(); // Impede lances futuros
        update();
        return;
    }
}

void CheckerBoard::onSetupModeChanged(bool inSetupMode)
{
    m_moveListWidget->setVisible(!inSetupMode);
}

void CheckerBoard::mousePressEvent(QMouseEvent *event)
{
    // Se o jogo terminou, ignora cliques no tabuleiro
    if (m_isGameOver) {
        return;
    }

    // No modo de análise, um clique do mouse interrompe a análise e joga o melhor lance.
    if (m_gameMode == GameMode::ANALYSIS) {
        stopAnalysisAndPlayBestMove();
        return;
    }

    // Se a IA está pensando, ignora todos os cliques para evitar interferência.
    // Isso previne que o usuário faça um lance enquanto a IA calcula.
    if (m_isAiThinking) {
        return;
    }

    // Apenas o botão esquerdo do mouse
    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (m_isSetupMode) {
        QPoint clickPos = event->pos();
        if (m_paletteRect.contains(clickPos)) {
            // Clique na paleta
            if (m_eraserToolRect.contains(clickPos)) m_setupTool = SetupTool::ERASER;
            else if (m_whitePawnToolRect.contains(clickPos)) m_setupTool = SetupTool::WHITE_PAWN;
            else if (m_blackPawnToolRect.contains(clickPos)) m_setupTool = SetupTool::BLACK_PAWN;
            else if (m_whiteKingToolRect.contains(clickPos)) m_setupTool = SetupTool::WHITE_KING;
            else if (m_blackKingToolRect.contains(clickPos)) m_setupTool = SetupTool::BLACK_KING;
            else if (m_clearBoardButtonRect.contains(clickPos)) {
                if (m_isClearButtonInClearState) { // Botão diz "Limpar Tabuleiro"
                    m_coreState.limpar_tabuleiro();
                } else { // Botão diz "Posição Inicial"
                    m_coreState.configurar_posicao_inicial();
                }
                m_isClearButtonInClearState = !m_isClearButtonInClearState; // Inverte o estado
            }
            else if (m_playerTurnButtonRect.contains(clickPos)) m_coreState.alternar_turno();
            else if (m_doneButtonRect.contains(clickPos)) {
                // Sair do modo de montagem
                m_isSetupMode = false;
                m_isGameOver = false; // Posição montada, o jogo (re)começa
                m_coreState.recalcular_hash_completo(); // Garante que o hash está correto
                resetHistory();
                calculatePossibleMoves();
                updateMoveListWidget();
                emit setupModeChanged(false);
                update(); // Redesenha sem a paleta
                checkGameOver(); // A posição pode ser de fim de jogo
            }
            update();
            return;
        }

        // Clique no tabuleiro
        const int visual_col = (clickPos.x() - m_marginSize) / m_squareSize;
        const int visual_row = (clickPos.y() - m_marginSize) / m_squareSize;
        if (visual_col >= 0 && visual_col < BoardSize && visual_row >= 0 && visual_row < BoardSize) {
            // No modo de montagem, só permite colocar peças nas casas escuras.
            // A soma de linha e coluna de uma casa escura é sempre ímpar.
            if ((visual_row + visual_col) % 2 == 0) {
                return; // Ignora clique em casa clara.
            }

            int logical_row = visual_row;
            int logical_col = visual_col;
            if (m_isBoardRotated) {
                logical_row = 7 - visual_row;
                logical_col = 7 - visual_col;
            }
            const int clickedSquare = logical_row * 8 + logical_col;

            switch (m_setupTool) {
                case SetupTool::ERASER: m_coreState.clearPieceAt(clickedSquare); break;
                case SetupTool::WHITE_PAWN: m_coreState.setPiece(clickedSquare, false, DamasCore::GamePlayer::BRANCAS); break;
                case SetupTool::BLACK_PAWN: m_coreState.setPiece(clickedSquare, false, DamasCore::GamePlayer::PRETOS); break;
                case SetupTool::WHITE_KING: m_coreState.setPiece(clickedSquare, true, DamasCore::GamePlayer::BRANCAS); break;
                case SetupTool::BLACK_KING: m_coreState.setPiece(clickedSquare, true, DamasCore::GamePlayer::PRETOS); break;
                default: break;
            }
            update();
        }
        return;
    }

    // Em modos que não envolvem um jogador humano, os cliques são ignorados.
    // A flag m_isAiThinking já cobre os momentos em que a IA está jogando.
    if (m_gameMode == GameMode::AI_VS_AI) {
        return;
    }
    // Converte a posição do clique em coordenadas do tabuleiro
    const int clickX = event->pos().x() - m_marginSize;
    const int clickY = event->pos().y() - m_marginSize;

    // Verifica se o clique foi dentro do tabuleiro
    if (clickX < 0 || clickY < 0 || clickX >= BoardSize * m_squareSize || clickY >= BoardSize * m_squareSize) {
        // Clicou fora, deseleciona tudo
        if (m_selectedSquare != -1) {
            m_selectedSquare = -1;
            m_movesForSelectedPiece.clear();
            update();
        }
        return;
    }

    const int visual_col = clickX / m_squareSize;
    const int visual_row = clickY / m_squareSize;

    int logical_row = visual_row;
    int logical_col = visual_col;
    if (m_isBoardRotated) {
        logical_row = 7 - visual_row;
        logical_col = 7 - visual_col;
    }
    const int clickedSquare = logical_row * 8 + logical_col;

    // 1. Se uma peça está selecionada, verificamos se o clique é um movimento válido.
    if (m_selectedSquare != -1) {
        for (const auto& move : m_movesForSelectedPiece) {
            if (move.casa_destino == clickedSquare) {
                executeMove(move);
                return; // Movimento executado, terminamos.
            }
        }
    }

    // 2. Se não foi um movimento, tratamos como uma nova tentativa de seleção.
    m_selectedSquare = -1; // Limpa seleção anterior para começar de novo.
    m_movesForSelectedPiece.clear();

    for (const auto& move : m_allPossibleMoves) {
        if (move.casa_origem == clickedSquare) {
            m_selectedSquare = clickedSquare; // Encontrou uma peça móvel, seleciona.
            break;
        }
    }

    if (m_selectedSquare != -1) {
        updateMovesForSelection(m_selectedSquare); // Popula os movimentos para a peça recém-selecionada.
    }

    update(); // Solicita um redesenho para mostrar a nova seleção (ou a falta dela).
}
