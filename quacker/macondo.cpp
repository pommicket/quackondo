#include "macondo.h"
#include "macondobackend.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>
#include <QTimer>

Macondo::Macondo(Quackle::Game *game) : QWidget() {
	m_updateTimer = new QTimer(this);
	QGridLayout *layout = new QGridLayout(this);
	m_simulateButton = new QPushButton(tr("Simulate"));
	layout->addWidget(m_simulateButton, 0, 0);
	layout->setAlignment(Qt::AlignTop);
	const char *home = getenv("HOME");
	// TODO: configurable path
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	MacondoBackend::InitOptions initOptions(execPath);
	m_backend = new MacondoBackend(game, initOptions);
	connect(m_simulateButton, SIGNAL(clicked()), this, SLOT(simulate()));
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(updateResults()));
	m_updateTimer->setInterval(1000);
}

void Macondo::simulate() {
	MacondoBackend::SimulateOptions options;
	m_backend->simulate(options);
	m_updateTimer->start();
}

void Macondo::updateResults() {
	switch (m_backend->command()) {
	case MacondoCommand::None:
		break;
	case MacondoCommand::Simulate:
		m_backend->getSimResults();
		break;
	case MacondoCommand::Solve:
		// TODO
		break;
	}
}
