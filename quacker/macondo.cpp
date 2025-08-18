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
	connectBackendSignals();
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
	if (m_isSolving) {
		// don't start a simulation if we're solving a (pre-)endgame
		return;
	}
	if (isRunning())
		stop();
	clearMoves();
	MacondoSimulateOptions options;
	m_backend->simulate(options, m_movesFromKibitzer);
}

bool Macondo::isRunning() const {
	return m_backend->isRunning();
}

void Macondo::solve() {
	bool wasSolving = m_isSolving;
	if (isRunning())
		stop();
	clearMoves();
	if (wasSolving) {
		emit stoppedSolver();
	} else {
		emit runningSolver();
		MacondoSolveOptions options;
		m_backend->solve(options);
		m_isSolving = true;
	}
	updateSolveButton();
}

void Macondo::gameChanged(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *initOptions);
	connectBackendSignals();
	m_game = game;
}

void Macondo::connectBackendSignals() {
	connect(m_backend, SIGNAL(gotMoves(const Quackle::MoveList &)), this, SLOT(gotMoves(const Quackle::MoveList &)));
	connect(m_backend, SIGNAL(statusMessage(const QString &)), this, SIGNAL(statusMessage(const QString &)));
}

void Macondo::stop() {
	m_backend->stop();
	m_anyUpdates = false;
	m_isSolving = false;
	updateSolveButton();
}

bool Macondo::useForSimulation() const {
	return m_useMacondo->isChecked();
}

void Macondo::gotMoves(const Quackle::MoveList &moves) {
	m_moves = moves;
	m_anyUpdates = true;
	if (m_isSolving && moves.size() > 0) {
		emit setCandidateMove(&moves[0]);
	}
}

void Macondo::positionChanged(const Quackle::GamePosition *position) {
	if (!isRunning()) {
		// perhaps new moves were generated
		m_movesFromKibitzer = position->moves();
	}
	m_tilesUnseen = position->gameOver() ? 0 : position->unseenBag().size();
	updateSolveButton();
}

// update "Solve" button text and enabledness*
void Macondo::updateSolveButton() {
	if (isRunning()) {
		m_solve->setText(tr("Stop"));
		m_solve->setDisabled(false);
	} else if (m_tilesUnseen > 0 && m_tilesUnseen <= 7) {
		m_solve->setText(tr("Solve endgame"));
		m_solve->setDisabled(false);
	} else if (m_tilesUnseen > 7 && m_tilesUnseen <= 10) {
		m_solve->setText(tr("Solve pre-endgame"));
		m_solve->setDisabled(false);
	} else {
		m_solve->setText(tr("Solve"));
		m_solve->setDisabled(true);
	}
}
