#include "macondo.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>
#include <QProcess>
#include <QTimer>
#include <random>

Macondo::Macondo(TopLevel *topLevel) : QWidget() {
	m_topLevel = topLevel;
	m_updateTimer = new QTimer(this);
	QGridLayout *layout = new QGridLayout(this);
	m_runButton = new QPushButton(tr("Run"));
	layout->addWidget(m_runButton, 0, 0);
	layout->setAlignment(Qt::AlignTop);
	const char *home = getenv("HOME");
	// TODO: configurable path
	m_execPath = home ? home : "/";
	m_execPath += "/apps/macondo/macondo";
	connect(m_runButton, SIGNAL(clicked()), this, SLOT(run()));
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(updateResults()));
	m_updateTimer->setInterval(100);
}

void Macondo::run() {
	if (m_process) return;
	printf("running macondo %s\n", m_execPath.c_str());
	m_process = new QProcess;
	QStringList args;
	m_process->start(m_execPath.c_str(), args);
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
}

void Macondo::processStarted() {
	m_updateTimer->start();
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
	std::string command = "load ";
	command += filename;
	command += "\n";
	m_process->write(command.c_str());
}

void Macondo::updateResults() {
	QProcess::ProcessState state = m_process->state();
	QByteArray data = m_process->readAllStandardError();
	printf("%s",data.constData());
	data = m_process->readAllStandardOutput();
	printf("%s", data.constData());
	fflush(stdout);
}
