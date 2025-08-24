/*
TODO:
- handle early exit from (pre-)endgame solve
*/

#include "macondo.h"
#include "macondobackend.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QOperatingSystemVersion>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

#include "customqsettings.h"

// Convenience wrapper around an hbox containing a label and another widget.
class LabelLayout: public QHBoxLayout {
public:
	LabelLayout(const QString &label, QWidget *widget): QHBoxLayout() {
		addWidget(new QLabel(label));
		addWidget(widget);
	}
};

Macondo::Macondo(Quackle::Game *game) : View() {
	CustomQSettings settings;
	m_game = game;
	QFont boldFont;
	boldFont.setWeight(QFont::Bold);
	m_useMacondo = new QCheckBox(tr("Use Macondo for 'Simulate'"));
	m_useMacondo->setChecked(settings.value("macondo/useForSimulate", false).toBool());
	QString defaultExecPath = QCoreApplication::applicationDirPath() + "/macondo/macondo";
	std::string execPath = settings.value("macondo/execPath", defaultExecPath).toString().toStdString();
	m_initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *m_initOptions);
	connectBackendSignals();
	QLabel *execPathLabel = new QLabel(tr("Macondo executable"));
	QPushButton *selectExecButton = new QPushButton(tr("Choose File..."));
	connect(selectExecButton, SIGNAL(clicked()), this, SLOT(chooseExecPath()));
	m_execPath = new QLineEdit;
	m_execPath->setText(QString::fromStdString(execPath));
	connect(m_execPath, SIGNAL(editingFinished()), this, SLOT(execPathChanged()));
	m_solve = new QPushButton(tr("Solve"));
	m_solve->setDisabled(true);
	m_log = new QPlainTextEdit;
	//m_log->setReadOnly(true);

	QHBoxLayout *execPathLayout = new QHBoxLayout;
	execPathLayout->addWidget(execPathLabel);
	execPathLayout->addWidget(m_execPath);
	execPathLayout->addWidget(selectExecButton);

	QGroupBox *pegBox = new QGroupBox(tr("Pre-endgame options"));
	QVBoxLayout *pegLayout = new QVBoxLayout;
	m_generatedMovesOnly = new QCheckBox(tr("Generated moves only"));
	m_generatedMovesOnly->setToolTip("Only analyze the moves that have been generated in the 'Choices' box.");
	m_generatedMovesOnly->setChecked(settings.value("macondo/generatedMovesOnly", false).toBool());
	m_preEndgameMaxPlies = new QSpinBox;
	m_preEndgameMaxPlies->setRange(1, 100);
	m_preEndgameMaxPlies->setValue(settings.value("macondo/preEndgameMaxPlies", 4).toInt());
	m_earlyCutoff = new QCheckBox(tr("Early cut-off"));
	m_earlyCutoff->setChecked(settings.value("macondo/earlyCutoff", true).toBool());
	m_earlyCutoff->setToolTip(tr("Cut off analysis if another play is certainly better."
		" This speeds up analysis but moves other than the top one might not be listed in the correct order."));
	m_skipNonEmptying = new QCheckBox(tr("Skip non-emptying"));
	m_skipNonEmptying->setToolTip(tr("Skip analyzing plays which don't empty the bag."
		" This speeds up analysis but may miss optimal non-emptying plays."));
	m_skipNonEmptying->setChecked(settings.value("macondo/skipNonEmptying", true).toBool());
	m_skipTieBreaker = new QCheckBox(tr("Skip tie-breaker"));
	m_skipTieBreaker->setToolTip(tr("Skip breaking win% ties by spread. "
		"This speeds up analysis but the top-ranked play might not have the best spread."));
	m_skipTieBreaker->setChecked(settings.value("macondo/skipTieBreaker", false).toBool());
	m_opponentRack = new QLineEdit;
	m_opponentRack->setToolTip("Enter any tiles which you know your opponent has here, for more accurate analysis.");

	pegLayout->addWidget(m_generatedMovesOnly);
	pegLayout->addWidget(m_earlyCutoff);
	pegLayout->addWidget(m_skipNonEmptying);
	pegLayout->addWidget(m_skipTieBreaker);
	pegLayout->addLayout(new LabelLayout(tr("Endgame plies"), m_preEndgameMaxPlies));
	pegLayout->addLayout(new LabelLayout(tr("Opponent rack"), m_opponentRack));
	pegBox->setLayout(pegLayout);

	QGroupBox *endgameBox = new QGroupBox(tr("Endgame options"));
	QVBoxLayout *endgameLayout = new QVBoxLayout;
	endgameBox->setLayout(endgameLayout);
	m_endgameMaxPlies = new QSpinBox;
	m_endgameMaxPlies->setRange(1, 100);
	m_endgameMaxPlies->setValue(settings.value("macondo/endgameMaxPlies", 15).toInt());
	m_firstWinOptimization = new QCheckBox(tr("First-win optimization"));
	m_firstWinOptimization->setToolTip(tr("Stop analysis as soon as a win is found. Speeds up analysis, but no longer ensures the best possible spread."));
	m_firstWinOptimization->setChecked(settings.value("macondo/firstWinOptimization", false).toBool());
	m_preventSlowRoll = new QCheckBox(tr("Prevent slow-roll"));
	m_preventSlowRoll->setChecked(settings.value("macondo/preventSlowRoll", false).toBool());
	m_preventSlowRoll->setToolTip(tr("Skip analyzing \"slow-plays\" when seemingly better options exist. This speeds up the solver, but may miss optimal endgame sequences."));
	endgameLayout->addLayout(new LabelLayout(tr("Plies"), m_endgameMaxPlies));
	endgameLayout->addWidget(m_firstWinOptimization);
	endgameLayout->addWidget(m_preventSlowRoll);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setAlignment(Qt::AlignTop);
	layout->addLayout(execPathLayout);
	layout->addWidget(m_useMacondo);
	layout->addWidget(pegBox);
	layout->addWidget(endgameBox);
	layout->addWidget(m_solve);
	layout->addWidget(m_log, 1);
	connect(m_solve, SIGNAL(clicked()), this, SLOT(solve()));
}

Macondo::~Macondo() {
	CustomQSettings settings;
	settings.setValue("macondo/useForSimulate", m_useMacondo->isChecked());
	settings.setValue("macondo/generatedMovesOnly", m_generatedMovesOnly->isChecked());
	settings.setValue("macondo/endgameMaxPlies", m_endgameMaxPlies->value());
	settings.setValue("macondo/preEndgameMaxPlies", m_preEndgameMaxPlies->value());
	settings.setValue("macondo/earlyCutoff", m_earlyCutoff->isChecked());
	settings.setValue("macondo/skipNonEmptying", m_skipNonEmptying->isChecked());
	settings.setValue("macondo/skipTieBreaker", m_skipTieBreaker->isChecked());
	settings.setValue("macondo/firstWinOptimization", m_firstWinOptimization->isChecked());
	settings.setValue("macondo/preventSlowRoll", m_preventSlowRoll->isChecked());
	
	delete m_backend;
}

void Macondo::setExecPath(const std::string &path) {
	m_backend->setExecPath(path);
	m_initOptions->execPath = path;
	QString qpath = QString::fromStdString(path);
	m_execPath->setText(qpath);
	CustomQSettings settings;
	settings.setValue("macondo/execPath", qpath);
}

void Macondo::chooseExecPath() {
	QString filter;
	if (QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows) {
		filter = tr("Executable files (*.exe)");
	}
	QString path = QFileDialog::getOpenFileName(this, tr("Select Macondo executable..."), QString(), filter);
	setExecPath(path.toStdString());
}

void Macondo::execPathChanged() {
	setExecPath(m_execPath->text().toStdString());
}

bool Macondo::checkExecPath() {
	const std::string &execPath = m_initOptions->execPath;
	if (execPath.empty()) {
		QMessageBox::critical(this,
			tr("Can't run Macondo"),
			tr("Please fill in the location of Macondo on your computer.")
		);
		return false;
	}
	if (!QFile::exists(QString::fromStdString(execPath))) {
		QString message = QString(tr("File %1 does not exist.")).arg(execPath.c_str());
		QMessageBox::critical(this,
			tr("Macondo not found"),
			message
		);
		return false;
	}
	return true;
}

bool Macondo::simulate() {
	if (!checkExecPath()) return false;
	if (m_isSolving) {
		// don't start a simulation if we're solving a (pre-)endgame
		return true;
	}
	if (isRunning())
		stop();
	clearLog();
	MacondoSimulateOptions options;
	return m_backend->simulate(options, m_movesFromKibitzer);
}

bool Macondo::isRunning() const {
	return m_backend->isRunning();
}

void Macondo::solve() {
	if (!checkExecPath()) return;
	bool wasSolving = m_isSolving;
	if (isRunning())
		stop();
	if (wasSolving) {
		emit stoppedSolver();
	} else {
		clearLog();
		if (m_tilesUnseen > 7) {
			MacondoPreEndgameOptions options;
			options.endgamePlies = m_preEndgameMaxPlies->value();
			options.earlyCutoff = m_earlyCutoff->isChecked();
			options.skipNonEmptying = m_skipNonEmptying->isChecked();
			options.skipTieBreaker = m_skipTieBreaker->isChecked();
			options.opponentRack = m_opponentRack->text().toStdString();
			if (m_generatedMovesOnly->isChecked()) {
				if (m_movesFromKibitzer.empty()) {
					QMessageBox::critical(this,
						tr("Can't run pre-endgame solver"),
						tr("Please generate moves to analyze or uncheck 'Generated moves only'")
					);
					return;
				}
				options.movesToAnalyze = m_movesFromKibitzer;
			}
			m_backend->solvePreEndgame(options);
		} else {
			MacondoEndgameOptions options;
			options.maxPlies = m_endgameMaxPlies->value();
			options.firstWinOptimization = m_firstWinOptimization->isChecked();
			options.preventSlowRoll = m_preventSlowRoll->isChecked();
			m_backend->solveEndgame(options);
		}
		m_isSolving = true;
		emit runningSolver();
	}
	updateSolveButton();
}

void Macondo::gameChanged(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *m_initOptions);
	connectBackendSignals();
	m_game = game;
	clearLog();
}

void Macondo::connectBackendSignals() {
	connect(m_backend, SIGNAL(gotMoves(const Quackle::MoveList &)), this, SLOT(gotMoves(const Quackle::MoveList &)));
	connect(m_backend, SIGNAL(statusMessage(const QString &)), this, SIGNAL(statusMessage(const QString &)));
	connect(m_backend, SIGNAL(newLogOutput(const QByteArray &)), this, SLOT(newLogOutput(const QByteArray &)));
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
	m_game->currentPosition().setMoves(moves);
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

void Macondo::clearLog() {
	m_log->clear();
	m_logANSIState = 0;
}

void Macondo::newLogOutput(const QByteArray &text) {
	// annoying stuff to handle incomplete UTF-8 at end of text
	QByteArray logContents = m_logPartialUtf8;
	logContents.append(text);
	size_t completeUtf8Len = 0;
	while (completeUtf8Len < logContents.length()) {
		uint8_t firstByte = logContents.at(completeUtf8Len);
		size_t codepointLen = (firstByte & 0xe0) == 0xc0 ? 2
			: (firstByte & 0xf0) == 0xe0 ? 3 
			: (firstByte & 0xf8) == 0xf0 ? 4 : 1;
		if (completeUtf8Len + codepointLen > logContents.length()) break;
		completeUtf8Len += codepointLen;
	}
	m_logPartialUtf8 = QByteArray(text.constData() + completeUtf8Len, text.length() - completeUtf8Len);
	// remove ANSI escape sequences (color/formatting codes)
	std::string filteredText;
	for (size_t i = 0; i < completeUtf8Len; i++) {
		uint8_t byte = text.at(i);
		if (m_logANSIState == 0 && byte == 0x1b) {
			m_logANSIState = 1;
		} else if (m_logANSIState == 1 && byte == '[') {
			m_logANSIState = 2;
		} else if (m_logANSIState == 2 && byte >= 0x20 && byte < 0x40) {
			// continuation of ANSI escape sequence
		} else if (m_logANSIState == 2 && byte >= 0x40 && byte < 0x80) {
			// terminator for ANSI escape sequence
			m_logANSIState = 0;
		} else {
			m_logANSIState = 0;
			filteredText.push_back(byte);
		}
	}
	m_log->moveCursor(QTextCursor::MoveOperation::End);
	m_log->insertPlainText(QString::fromStdString(filteredText));
	m_log->moveCursor(QTextCursor::MoveOperation::End);
	m_log->centerCursor();
}
