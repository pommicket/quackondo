#include "macondobackend.h"
#include "quackleio/gcgio.h"
#include "game.h"

#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QTextStream>
#include <random>

static int getPlyNumber(const Quackle::GamePosition &position) {
	int playerIndex = 0, numPlayers = position.players().size();
	for (const Quackle::Player &player: position.players()) {
		if (player.id() == position.playerOnTurn().id()) {
			break;
		}
		playerIndex++;
	}
	if (playerIndex >= numPlayers) {
		throw "couldn't find player in player list";
	}
	return (position.turnNumber() - 1) * numPlayers
		+ playerIndex;
}


MacondoBackend::MacondoBackend(Quackle::Game *game, const InitOptions &options) {
	m_execPath = options.execPath;
	m_game = game;
	m_updateTimer = new QTimer(this);
	m_updateTimer->setInterval(1000);
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(timer()));
	m_updateTimer->start();
}

void MacondoBackend::simulate(const SimulateOptions &) {
	if (m_process) return;
	printf("running macondo %s\n", m_execPath.c_str());
	m_process = new QProcess(this);
	QStringList args;
	m_process->start(m_execPath.c_str(), args);
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	m_command = Command::Simulate;
}

void MacondoBackend::timer() {
	if (m_process) {
		QByteArray data = m_process->readAllStandardError();
		fprintf(stderr,"%s",data.constData());
		data = m_process->readAllStandardOutput();
		m_processOutput.append(data);
		fflush(stdout);
	}
	switch (m_command) {
	case Command::None:
		break;
	case Command::Simulate:
		if (m_runningSimulation) {
			m_process->write("sim show\n");
		}
		{
			QByteArray playsStartIdentifier("Play                Leave         Score    Win%            Equity");
			QByteArray playsEndIdentifier("Iterations:");
			int start = m_processOutput.indexOf(playsStartIdentifier) + playsStartIdentifier.length();
			if (start < 0) return;
			// trim whitespace before plays
			while (start < m_processOutput.length()
				&& strchr(" \r\n", m_processOutput[start])) {
				start++;
			}
			int end = m_processOutput.indexOf(playsEndIdentifier, start);
			if (end < 0) return;
			std::string plays(m_processOutput.constData() + start, end - start);
			m_processOutput.remove(0, end);
			printf("%s\n",plays.c_str());
		}
		break;
	case Command::Solve:
		// TODO
		break;
	}
}

void MacondoBackend::processStarted() {
	loadGCG();
	switch (m_command) {
	case Command::None:
		throw "process started with no command";
	case Command::Simulate:
		m_process->write("gen\nsim\n");
		m_runningSimulation = true;
		break;
	case Command::Solve:
		// TODO
		break;
	}
}

void MacondoBackend::loadGCG() {
	std::random_device randDev;
	std::mt19937 rand(randDev());
	std::uniform_int_distribution<int> distribution(0, 26);
	if (!m_tempGCG.empty()) {
		remove(m_tempGCG.c_str());
	}
	// save game file with random name
	char filename[] = "tmpGameXXXXXXXXXXXX.gcg";
	for (int i = 0; filename[i]; i++) {
		if (filename[i] == 'X') {
			filename[i] = distribution(rand) + 'A';
		}
	}
	m_tempGCG = filename;
	QuackleIO::GCGIO gcg;
	{
		QFile file(filename);
		file.open(QIODevice::WriteOnly | QIODevice::Text);
		QTextStream fileStream(&file);
		gcg.write(*m_game, fileStream);
	}
	std::stringstream commands;
	commands << "load " << filename << "\n"
		<< "turn " << getPlyNumber(m_game->currentPosition()) << "\n";
	m_process->write(commands.str().c_str());
}

void MacondoBackend::killProcess() {
	if (m_process) {
		m_process->kill();
		m_process->deleteLater();
		m_process = nullptr;
	}
}

void MacondoBackend::processFinished(int, QProcess::ExitStatus) {
}

MacondoBackend::~MacondoBackend() {
	if (m_process) {
		m_process->kill();
	}
}
