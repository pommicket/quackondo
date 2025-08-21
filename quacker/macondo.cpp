/*
TODO:
- configurable execPath
     - Choose file... button
- save options
- detect Macondo crashing?
- configurable max plies
- other peg/endgame options
*/

#include "macondo.h"
#include "macondobackend.h"

#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QLineEdit>

Macondo::Macondo(Quackle::Game *game) : View() {
	m_game = game;
	QFont boldFont;
	boldFont.setWeight(QFont::Bold);
	m_useMacondo = new QCheckBox(tr("Use Macondo for 'Simulate'"));
	const char *home = getenv("HOME");
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	m_initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *m_initOptions);
	connectBackendSignals();
	QLabel *execPathLabel = new QLabel(tr("Macondo executable"));
	QPushButton *selectExecButton = new QPushButton(tr("Choose File..."));
	m_execPath = new QLineEdit;
	m_solve = new QPushButton(tr("Solve"));
	m_solve->setDisabled(true);
	QGroupBox *pegBox = new QGroupBox(tr("Pre-endgame options"));
	QVBoxLayout *pegLayout = new QVBoxLayout;
	m_generatedMovesOnly = new QCheckBox(tr("Generated moves only"));
	m_generatedMovesOnly->setToolTip("Only analyze the moves that have been generated in the 'Choices' box.");
	pegLayout->addWidget(m_generatedMovesOnly);
	pegBox->setLayout(pegLayout);
	QHBoxLayout *execPathLayout = new QHBoxLayout;
	execPathLayout->addWidget(execPathLabel);
	execPathLayout->addWidget(m_execPath);
	execPathLayout->addWidget(selectExecButton);
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setAlignment(Qt::AlignTop);
	layout->addLayout(execPathLayout);
	layout->addWidget(m_useMacondo);
	layout->addWidget(pegBox);
	layout->addWidget(m_solve);
	connect(m_solve, SIGNAL(clicked()), this, SLOT(solve()));
	connect(m_execPath, SIGNAL(editingFinished()), this, SLOT(newExecPath()));
}

Macondo::~Macondo() {
	delete m_backend;
}

void Macondo::newExecPath() {
	std::string newPath = m_execPath->text().toStdString();
	m_backend->setExecPath(newPath);
	m_initOptions->execPath = newPath;
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
		if (m_tilesUnseen > 7) {
			MacondoPreEndgameOptions options;
			if (m_generatedMovesOnly->isChecked()) {
				if (m_movesFromKibitzer.empty()) {
					QMessageBox::critical(this, tr("Can't run pre-endgame solver"), tr("Please generate moves to analyze or uncheck 'Generated moves only'"));
					return;
				}
				options.movesToAnalyze = m_movesFromKibitzer;
			}
			m_backend->solvePreEndgame(options);
		} else {
			MacondoEndgameOptions options;
			m_backend->solveEndgame(options);
		}
		emit runningSolver();
		m_isSolving = true;
	}
	updateSolveButton();
}

void Macondo::gameChanged(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *m_initOptions);
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
