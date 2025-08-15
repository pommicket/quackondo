/*
TODO:
- configurable execPath
- detect Macondo crashing?
- stop Macondo solve when game position changes
- configurable max plies
*/

#include "macondo.h"
#include "macondobackend.h"

#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>

Macondo::Macondo(Quackle::Game *game) : View() {
	m_game = game;
	m_useMacondo = new QCheckBox(tr("Use Macondo for 'Simulate'"));
	const char *home = getenv("HOME");
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *initOptions);
	connect(m_backend, SIGNAL(gotSimMoves(const Quackle::MoveList &)), this, SLOT(gotSimMoves(const Quackle::MoveList &)));
	m_solve = new QPushButton(tr("Solve"));
	m_solve->setDisabled(true);
	QGridLayout *layout = new QGridLayout(this);
	layout->setAlignment(Qt::AlignTop);
	layout->addWidget(m_useMacondo, 0, 0);
	layout->addWidget(m_solve, 1, 0);
	connect(m_solve, SIGNAL(clicked()), this, SLOT(solve()));
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

void Macondo::solve() {
	bool wasSolving = m_isSolving;
	if (m_backend->isRunning())
		stop();
	clearMoves();
	if (wasSolving) {
		m_solve->setText(tr("Solve"));
	} else {
		MacondoSolveOptions options;
		m_backend->solve(options);
		m_solve->setText(tr("Stop"));
		m_isSolving = true;
	}
}

void Macondo::gameChanged(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *initOptions);
	m_game = game;
}

void Macondo::stop() {
	m_backend->stop();
	m_anyUpdates = false;
	m_isSolving = false;
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
	int tilesLeft = position->unseenBag().size();
	if (!position->gameOver() && tilesLeft <= 7) {
		m_solve->setText(tr("Solve endgame"));
		m_solve->setDisabled(false);
	} else if (!position->gameOver() && tilesLeft <= 10) {
		m_solve->setText(tr("Solve pre-endgame"));
		m_solve->setDisabled(false);
	} else {
		m_solve->setText(tr("Solve"));
		m_solve->setDisabled(true);
	}
}
