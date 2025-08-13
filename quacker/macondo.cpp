#include "macondo.h"
#include "macondobackend.h"
#include "movebox.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>

Macondo::Macondo(Quackle::Game *game, MoveBox *moveBox) : View() {
	m_game = game;
	m_moveBox = moveBox;
	QGridLayout *layout = new QGridLayout(this);
	m_useMacondo = new QCheckBox(tr("Use Macondo for 'Simulate'"));
	layout->setAlignment(Qt::AlignTop);
	layout->addWidget(m_useMacondo, 0, 0);
	const char *home = getenv("HOME");
	// TODO: configurable path
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *initOptions);
	connect(m_backend, SIGNAL(gotSimMoves(const Quackle::MoveList *)), this, SIGNAL(newMoves(const Quackle::MoveList *)));
}

Macondo::~Macondo() {
	delete m_backend;
}

void Macondo::simulate() {
	if (m_backend->isRunning()) {
		// stop analysis
		stop();
		return;
	}
	MacondoSimulateOptions options;
	m_backend->simulate(options);
}

void Macondo::setGame(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *initOptions);
	m_game = game;
}

void Macondo::stop() {
	m_backend->stop();
}

bool Macondo::useForSimulation() const {
	return m_useMacondo->isChecked();
}
