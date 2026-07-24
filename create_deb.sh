#!/bin/bash

# Encontra o diretório onde o script está localizado para tornar os caminhos robustos
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Configurações do pacote
PKG_NAME="lib-engine"
VERSION="1.4"
# Arquitetura alvo do pacote
ARCH="amd64"
BUILD_DIR="${PKG_NAME}_${VERSION}_${ARCH}"

echo "=> Limpando empacotamentos antigos..."
rm -rf ${BUILD_DIR}
rm -f ${BUILD_DIR}.deb

echo "=> Criando arvore de diretorios do pacote..."
mkdir -p ${BUILD_DIR}/DEBIAN
mkdir -p ${BUILD_DIR}/opt/${PKG_NAME}/data
mkdir -p ${BUILD_DIR}/opt/${PKG_NAME}/audio
mkdir -p ${BUILD_DIR}/opt/${PKG_NAME}/db
mkdir -p ${BUILD_DIR}/opt/${PKG_NAME}/img
mkdir -p ${BUILD_DIR}/usr/share/applications

echo "=> Criando arquivo de controle (metadata)..."
cat <<EOF > ${BUILD_DIR}/DEBIAN/control
Package: ${PKG_NAME}
Version: ${VERSION}
Section: games
Priority: optional
Architecture: ${ARCH}
Depends: libqt6widgets6, libqt6gui6, libqt6core6, libzstd1, alsa-utils
Maintainer: Emanoel Libonati <brasillinux20@gmail.com>
Description: LIB Engine - Jogo de Damas com IA
 Interface gráfica e motor de Inteligência Artificial para o Jogo de Damas na regra brasileira.
 Possui aprendizado contínuo (Reinforcement Learning) e suporte a Base de Finais (EGTB).
EOF

echo "=> Criando script preinst (Backup da pasta data)..."
cat <<'EOF' > ${BUILD_DIR}/DEBIAN/preinst
#!/bin/bash
# Move a pasta 'data' existente para um local temporario antes da instalacao/upgrade
# para preservar o aprendizado da IA.
# O script 'postinst' ira restaurar os dados.

# $1 sera 'install' ou 'upgrade'
if [ "$1" = "upgrade" ] || [ "$1" = "install" ]; then
    # Verifica se a pasta de dados antiga existe
    if [ -d "/opt/lib-engine/data" ]; then
        echo "Preservando dados de aprendizado da IA..."
        # Move a pasta para um nome temporario. Ignora erros se nao conseguir.
        mv /opt/lib-engine/data /opt/lib-engine/data.old 2>/dev/null || true
    fi
fi

exit 0
EOF
chmod 755 ${BUILD_DIR}/DEBIAN/preinst

echo "=> Criando script postinst (Restaura dados e libera acesso)..."
cat <<'EOF' > ${BUILD_DIR}/DEBIAN/postinst
#!/bin/bash
# Restaura os dados de aprendizado da IA e da permissao as pastas.

# $1 sera 'configure'
if [ "$1" = "configure" ]; then
    # Verifica se o backup dos dados existe
    if [ -d "/opt/lib-engine/data.old" ]; then
        echo "Restaurando dados de aprendizado da IA..."
        # Copia o conteudo do backup para a nova pasta de dados.
        cp -a /opt/lib-engine/data.old/* /opt/lib-engine/data/ 2>/dev/null || true
        # Remove a pasta de backup
        rm -rf /opt/lib-engine/data.old
    fi
fi

# Da permissao total (777) as pastas de dados e EGTB para a IA evoluir sem erro de permissao.
chmod -R 777 /opt/lib-engine/data 2>/dev/null || true
chmod -R 777 /opt/lib-engine/db 2>/dev/null || true
exit 0
EOF
chmod 755 ${BUILD_DIR}/DEBIAN/postinst

echo "=> Copiando o projeto compilado e assets..."
cp "${SCRIPT_DIR}/build/lib_engine" "${BUILD_DIR}/opt/${PKG_NAME}/"
cp "${SCRIPT_DIR}/audio/move.wav" "${BUILD_DIR}/opt/${PKG_NAME}/audio/"
cp "${SCRIPT_DIR}/img/libo.png" "${BUILD_DIR}/opt/${PKG_NAME}/img/" 2>/dev/null || true

# Copia os dados de aprendizado e as bases de finais, se existirem.
# O 'if' evita erros se as pastas não existirem ou estiverem vazias.
if [ -d "${SCRIPT_DIR}/../db" ] && [ "$(ls -A ${SCRIPT_DIR}/../db)" ]; then
    cp -r "${SCRIPT_DIR}/../db/"* "${BUILD_DIR}/opt/${PKG_NAME}/db/"
fi

echo "=> Criando atalho de menu do Linux (.desktop)..."
cat <<EOF > ${BUILD_DIR}/usr/share/applications/${PKG_NAME}.desktop
[Desktop Entry]
Version=${VERSION}
Name=LIB Engine
Comment=LIB Engine - Motor de Damas com IA
Exec=/opt/${PKG_NAME}/lib_engine
Path=/opt/${PKG_NAME}/
Icon=/opt/${PKG_NAME}/img/libo.png
Terminal=false
Type=Application
Categories=Game;BoardGame;StrategyGame;
EOF

echo "=> Empacotando com dpkg-deb..."
dpkg-deb --build ${BUILD_DIR}

echo "=> Concluido! O pacote ${BUILD_DIR}.deb foi gerado na sua pasta."
echo "Para instala-lo no seu sistema, use o comando 'dpkg':"
echo "sudo dpkg -i ./${BUILD_DIR}.deb"
echo "Se houver erros de dependência, resolva-os com: sudo apt -f install"
echo ""
echo "Para desinstalar, use o comando: sudo apt remove ${PKG_NAME}"
