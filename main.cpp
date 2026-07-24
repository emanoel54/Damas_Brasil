#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include "checkerboard.h"
#include "damas_ai.hpp"
#include "damas_core.hpp"
#include <QIcon> // Para QIcon
#include <QMessageBox> // Para a caixa de diálogo "Sobre"
#include <QPixmap>     // Para usar imagens na caixa "Sobre"
#include <QActionGroup>

int main(int argc, char *argv[])
{
    // --- INICIALIZAÇÃO DA APLICAÇÃO ---
    // Cria a instância da aplicação Qt
    QApplication app(argc, argv);

    // Define um estilo para os separadores de menu para torná-los mais visíveis
    app.setStyleSheet("QMenu::separator { height: 2px; background: lightgray; margin-left: 10px; margin-right: 5px; }");
    
    // Inicializa as chaves Zobrist para o motor do jogo
    DamasCore::init_zobrist_keys();

    // Inicializa os recursos da IA (tabelas, etc.)
    DamasAI::inicializar_ia();

    // Cria a janela principal da aplicação
    QMainWindow mainWindow;
    mainWindow.setWindowTitle("LIB Engine v1.4");

    // Define o ícone da janela
    mainWindow.setWindowIcon(QIcon(":/img/libo.png"));
    
    // Cria a barra de menus e adiciona os menus solicitados
    QMenuBar *menuBar = mainWindow.menuBar();
    QMenu *arquivoMenu = menuBar->addMenu("Arquivo");
    QAction *newGameAction = arquivoMenu->addAction("Novo Jogo");
    QAction *openGameAction = arquivoMenu->addAction("Abrir Jogo...");
    QAction *saveGameAction = arquivoMenu->addAction("Salvar Jogo...");
    arquivoMenu->addSeparator();
    QAction *openBatchAction = arquivoMenu->addAction("Abrir em Lote...");
    arquivoMenu->addSeparator();
    QAction *openPositionAction = arquivoMenu->addAction("Abrir Posição...");
    QAction *savePositionAction = arquivoMenu->addAction("Salvar Posição...");
    arquivoMenu->addSeparator();
    QAction *quitAction = arquivoMenu->addAction("Sair");

    QMenu *tabuleiroMenu = menuBar->addMenu("Tabuleiro");

    QMenu *resizeMenu = tabuleiroMenu->addMenu("Redimensionar");
    QActionGroup *sizeGroup = new QActionGroup(resizeMenu);
    sizeGroup->setExclusive(true);

    QAction *smallAction = resizeMenu->addAction("Pequeno");
    smallAction->setCheckable(true);
    smallAction->setChecked(true);
    sizeGroup->addAction(smallAction);

    QAction *largeAction = resizeMenu->addAction("Grande");
    largeAction->setCheckable(true);
    sizeGroup->addAction(largeAction);

    QAction *rotateAction = tabuleiroMenu->addAction("Girar");
    QAction *setupAction = tabuleiroMenu->addAction("Montar");
    tabuleiroMenu->addSeparator();
    QAction *undoAction = tabuleiroMenu->addAction("Voltar <<");
    undoAction->setShortcut(QKeySequence(Qt::Key_Left));
    QAction *redoAction = tabuleiroMenu->addAction("Avançar >>");
    redoAction->setShortcut(QKeySequence(Qt::Key_Right));

    QMenu *modoMenu = menuBar->addMenu("Modo");
    QActionGroup *modeGroup = new QActionGroup(modoMenu);
    modeGroup->setExclusive(true); // Garante que apenas um item seja marcado por vez

    QAction *hvhAction = modoMenu->addAction("Hum vs Hum");
    hvhAction->setShortcut(QKeySequence("a"));
    hvhAction->setCheckable(true);
    hvhAction->setChecked(true); // Define este como o modo padrão
    modeGroup->addAction(hvhAction);

    QAction *hvcAction = modoMenu->addAction("Hum vs IA");
    hvcAction->setShortcut(QKeySequence("s"));
    hvcAction->setCheckable(true);
    modeGroup->addAction(hvcAction);

    QAction *cvhAction = modoMenu->addAction("IA vs Hum");
    cvhAction->setShortcut(QKeySequence("d"));
    cvhAction->setCheckable(true);
    modeGroup->addAction(cvhAction);

    QAction *cvcAction = modoMenu->addAction("IA vs IA");
    cvcAction->setShortcut(QKeySequence("f"));
    cvcAction->setCheckable(true);
    modeGroup->addAction(cvcAction);

    QAction *analiseAction = modoMenu->addAction("Análise");
    analiseAction->setShortcut(QKeySequence("g"));
    analiseAction->setCheckable(true);
    modeGroup->addAction(analiseAction);

    QMenu *nivelMenu = menuBar->addMenu("Nível");
    QActionGroup *levelGroup = new QActionGroup(nivelMenu);
    levelGroup->setExclusive(true);

    QAction *inicianteAction = nivelMenu->addAction("Iniciante");
    inicianteAction->setCheckable(true);
    inicianteAction->setChecked(true); // Define como nível padrão
    levelGroup->addAction(inicianteAction);

    QAction *experienteAction = nivelMenu->addAction("Experiente");
    experienteAction->setCheckable(true);
    levelGroup->addAction(experienteAction);

    QAction *candidatoAction = nivelMenu->addAction("Candidato");
    candidatoAction->setCheckable(true);
    levelGroup->addAction(candidatoAction);

    QAction *mestreNacionalAction = nivelMenu->addAction("Mestre Nacional");
    mestreNacionalAction->setCheckable(true);
    levelGroup->addAction(mestreNacionalAction);

    QAction *mestreInternacionalAction = nivelMenu->addAction("Mestre Internacional");
    mestreInternacionalAction->setCheckable(true);
    levelGroup->addAction(mestreInternacionalAction);

    QAction *gmiAction = nivelMenu->addAction("GMI");
    gmiAction->setCheckable(true);
    levelGroup->addAction(gmiAction);

    menuBar->addMenu("Rede");
    
    QMenu *aprenderMenu = menuBar->addMenu("Aprender");
    QAction *correctAction = aprenderMenu->addAction("Corrigir Lance da IA");
    correctAction->setShortcut(QKeySequence("Ctrl+L"));
    QAction *addToBookAction = aprenderMenu->addAction("Adicionar ao Livro...");
    addToBookAction->setShortcut(QKeySequence("Ctrl+B"));
    
    QMenu *ajudaMenu = menuBar->addMenu("Ajuda");
    QAction *aboutAction = ajudaMenu->addAction("Sobre...");
    
    // Cria nosso widget de tabuleiro e o define como o widget central da janela
    CheckerBoard *board = new CheckerBoard(&mainWindow);
    mainWindow.setCentralWidget(board);
    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
    board->setUndoRedoActions(undoAction, redoAction);
    mainWindow.setFixedSize(mainWindow.sizeHint()); // Ajusta o tamanho e o torna fixo

    // --- Conexões de Sinais e Slots ---

    // Conecta a ação "Novo Jogo" ao slot correspondente no tabuleiro
    QObject::connect(newGameAction, &QAction::triggered, board, &CheckerBoard::startNewGame);

    // Conecta a ação "Abrir Jogo"
    QObject::connect(openGameAction, &QAction::triggered, board, &CheckerBoard::openGame);

    // Conecta a ação "Abrir em Lote"
    QObject::connect(openBatchAction, &QAction::triggered, board, &CheckerBoard::openBatch);

    // Conecta a ação "Salvar Jogo"
    QObject::connect(saveGameAction, &QAction::triggered, board, &CheckerBoard::saveGame);

    // Conecta as ações de Posição
    QObject::connect(openPositionAction, &QAction::triggered, board, &CheckerBoard::openPosition);
    QObject::connect(savePositionAction, &QAction::triggered, board, &CheckerBoard::savePosition);

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    // Conecta a ação "Montar" para entrar no modo de montagem
    QObject::connect(setupAction, &QAction::triggered, board, &CheckerBoard::enterSetupMode);

    // Conecta a ação "Girar"
    QObject::connect(rotateAction, &QAction::triggered, board, &CheckerBoard::rotateBoard);

    // Conecta as ações de Voltar/Avançar
    QObject::connect(undoAction, &QAction::triggered, board, &CheckerBoard::undoMove);
    QObject::connect(redoAction, &QAction::triggered, board, &CheckerBoard::redoMove);

    // Conecta as ações de redimensionamento
    QObject::connect(smallAction, &QAction::triggered, board, [board](){ board->setScale(1.0); });
    QObject::connect(largeAction, &QAction::triggered, board, [board](){ board->setScale(1.3); });

    // Conexões para o modo de jogo
    QObject::connect(hvhAction, &QAction::triggered, board, [board](){ board->setGameMode(CheckerBoard::GameMode::HUMAN_VS_HUMAN); });
    QObject::connect(hvcAction, &QAction::triggered, board, [board](){ board->setGameMode(CheckerBoard::GameMode::HUMAN_VS_AI); });
    QObject::connect(cvhAction, &QAction::triggered, board, [board](){ board->setGameMode(CheckerBoard::GameMode::AI_VS_HUMAN); });
    QObject::connect(cvcAction, &QAction::triggered, board, [board](){ board->setGameMode(CheckerBoard::GameMode::AI_VS_AI); });
    QObject::connect(analiseAction, &QAction::triggered, board, [board](){ board->setGameMode(CheckerBoard::GameMode::ANALYSIS); });

    // Conexões para o nível da IA
    auto setLevel = [board](int level) { return [board, level]() { board->setAiLevel(level); }; };
    QObject::connect(inicianteAction, &QAction::triggered, setLevel(0));
    QObject::connect(experienteAction, &QAction::triggered, setLevel(1));
    QObject::connect(candidatoAction, &QAction::triggered, setLevel(2));
    QObject::connect(mestreNacionalAction, &QAction::triggered, setLevel(3));
    QObject::connect(mestreInternacionalAction, &QAction::triggered, setLevel(4));
    QObject::connect(gmiAction, &QAction::triggered, setLevel(5));

    // Conexão para o menu Aprender
    QObject::connect(correctAction, &QAction::triggered, board, &CheckerBoard::correctAiMove);

    // Conexão para o Livro de Aberturas
    QObject::connect(addToBookAction, &QAction::triggered, board, &CheckerBoard::addCurrentMoveToBook);

    // Conecta o sinal para atualizar o menu quando o modo de jogo muda programaticamente (ex: ao sair do modo análise)
    QObject::connect(board, &CheckerBoard::gameModeChanged, [=](CheckerBoard::GameMode mode) {
        if (mode == CheckerBoard::GameMode::HUMAN_VS_HUMAN) hvhAction->setChecked(true);
        else if (mode == CheckerBoard::GameMode::HUMAN_VS_AI) hvcAction->setChecked(true);
        else if (mode == CheckerBoard::GameMode::AI_VS_HUMAN) cvhAction->setChecked(true);
        else if (mode == CheckerBoard::GameMode::AI_VS_AI) cvcAction->setChecked(true);
        else if (mode == CheckerBoard::GameMode::ANALYSIS) analiseAction->setChecked(true);
    });

    // Conecta a ação "Sobre" para mostrar a caixa de diálogo
    QObject::connect(aboutAction, &QAction::triggered, [&mainWindow]() {
        QMessageBox aboutBox(&mainWindow);
        aboutBox.setWindowTitle("Sobre LIB Engine v1.4");
        aboutBox.setIconPixmap(QPixmap(":/img/libo.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));

        QString aboutText =
            "<h3>LIB Engine v1.4</h3>"
            "<p>Interface gráfica e motor de Inteligência Artificial para o Jogo de Damas na regra brasileira (64 casas).</p>"
            "<h4>Recursos:</h4>"
            "<ul>"
            "<li><b>Modos de Jogo:</b> Jogue contra outro humano ou contra a IA em diversos níveis.</li>"
            "<li><b>Análise:</b> Use o poder da IA para analisar posições e encontrar os melhores lances.</li>"
            "<li><b>Montagem:</b> Crie qualquer cenário no tabuleiro para estudo ou treinamento.</li>"
            "<li><b>PDN/FEN:</b> Salve e carregue jogos e posições, compatível com outros softwares.</li>"
            "</ul>"
            "<p>Este é um software livre, distribuído sob os termos da Licença Pública Geral GNU v3.0.</p>"
            "<hr>"
            "<h4>Créditos</h4>"
            "<p><b>Desenvolvimento:</b><br>"
            "Emanoel Libonati<br>"
            "Ezequiel Libonati</p>"
            "<hr>"
            "<h4>Apoie o Projeto</h4>"
            "<p>Se você gostou deste software, considere fazer uma doação.</p>"
            "<p><b>Chave PIX:</b> brasillinux20@gmail.com</p>";

        aboutBox.setTextFormat(Qt::RichText);
        aboutBox.setText(aboutText);
        aboutBox.exec();
    });

    // Desativa/ativa os menus quando entra/sai do modo de montagem
    QList<QMenu*> menusToDisable = {arquivoMenu, modoMenu, nivelMenu, menuBar->findChild<QMenu*>("Rede"), menuBar->findChild<QMenu*>("Aprender"), menuBar->findChild<QMenu*>("Ajuda")};
    QObject::connect(board, &CheckerBoard::setupModeChanged, [menusToDisable, tabuleiroMenu](bool inSetupMode) {
        for (auto* menu : menusToDisable) { if(menu) menu->setEnabled(!inSetupMode); }
        // No menu tabuleiro, desativa tudo exceto a própria ação "Montar" (ou o menu inteiro)
        tabuleiroMenu->setEnabled(!inSetupMode);
    });

    mainWindow.show(); // Exibe a janela
    
    // Inicia o loop de eventos da aplicação e retorna o código de saída
    return app.exec();
}