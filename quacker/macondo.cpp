/*
TODO:
- configurable execPath
- set Macondo lexicon based on game
*/

#include "macondo.h"
#include "macondobackend.h"

#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>

Macondo::Macondo(Quackle::Game *game) : View() {
	m_game = game;
	QGridLayout *layout = new QGridLayout(this);
	m_useMacondo = new QCheckBox(tr("Use Macondo for 'Simulate'"));
	layout->setAlignment(Qt::AlignTop);
	layout->addWidget(m_useMacondo, 0, 0);
	const char *home = getenv("HOME");
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *initOptions);
	connect(m_backend, SIGNAL(gotSimMoves(const Quackle::MoveList &)), this, SLOT(gotSimMoves(const Quackle::MoveList &)));
}

Macondo::~Macondo() {
	delete m_backend;
}

void Macondo::simulate() {
	if (m_backend->isRunning())
		stop();
	clearMoves();
	MacondoSimulateOptions options;
	m_backend->simulate(options, m_movesFromKibitzer);
}

void Macondo::gameChanged(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *initOptions);
	m_game = game;
}

void Macondo::stop() {
	m_backend->stop();
	m_anyUpdates = false;
}

bool Macondo::useForSimulation() const {
	return m_useMacondo->isChecked();
}

void Macondo::gotSimMoves(const Quackle::MoveList &moves) {
	m_moves = moves;
	m_anyUpdates = true;
}

void Macondo::positionChanged(const Quackle::GamePosition *position) {
	if (!m_backend->isRunning()) {
		// perhaps new moves were generated
		m_movesFromKibitzer = position->moves();
	}
}
