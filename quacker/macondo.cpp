#include "macondo.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>
#include <QProcess>
#include <QTimer>
#include <random>

Macondo::Macondo(TopLevel *topLevel) : View() {
	m_topLevel = topLevel;
	m_updateTimer = new QTimer(this);
	QGridLayout *layout = new QGridLayout(this);
	m_simulateButton = new QPushButton(tr("Simulate"));
	layout->addWidget(m_simulateButton, 0, 0);
	layout->setAlignment(Qt::AlignTop);
	const char *home = getenv("HOME");
	// TODO: configurable path
	m_execPath = home ? home : "/";
	m_execPath += "/apps/macondo/macondo";
	connect(m_simulateButton, SIGNAL(clicked()), this, SLOT(simulate()));
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(updateResults()));
	m_updateTimer->setInterval(1000);
}

void Macondo::simulate() {
	if (m_process) return;
	printf("running macondo %s\n", m_execPath.c_str());
	m_process = new QProcess;
	QStringList args;
	m_process->start(m_execPath.c_str(), args);
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	m_command = Command::Simulate;
}

void Macondo::processStarted() {
	m_updateTimer->start();
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

void Macondo::killProcess() {
	if (m_process) {
		m_process->kill();
	}
}

void Macondo::processFinished(int, QProcess::ExitStatus) {
	delete m_process;
	m_process = nullptr;
}

Macondo::~Macondo() {
	killProcess();
}

void Macondo::loadGCG() {
	std::default_random_engine rand;
	std::uniform_int_distribution<int> distribution(0, 26);
	// save game file with random name
	char filename[] = "tmpGameXXXXXXXXXXXX.gcg";
	for (int i = 0; filename[i]; i++) {
		if (filename[i] == 'X') {
			filename[i] = distribution(rand) + 'A';
		}
	}
	m_topLevel->writeFile(filename);
	std::stringstream commands;
	commands << "load " << filename << "\n"
		<< "turn " << m_viewingPlyNumber << "\n";
	m_process->write(commands.str().c_str());
}

void Macondo::updateResults() {
	if (m_runningSimulation) {
		m_process->write("sim show\n");
	}
	QByteArray data = m_process->readAllStandardError();
	printf("%s",data.constData());
	data = m_process->readAllStandardOutput();
	printf("%s", data.constData());
	fflush(stdout);
}

void Macondo::positionChanged(const Quackle::GamePosition *position) {
	int playerIndex = 0, numPlayers = position->players().size();
	for (const Quackle::Player &player: position->players()) {
		if (player.id() == position->playerOnTurn().id()) {
			break;
		}
		playerIndex++;
	}
	if (playerIndex >= numPlayers) {
		throw "couldn't find player in player list";
	}
	m_viewingPlyNumber = (position->turnNumber() - 1) * numPlayers
		+ playerIndex;
}
