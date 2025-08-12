#include "macondo.h"
#include "macondobackend.h"
#include "movebox.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>

Macondo::Macondo(Quackle::Game *game) : QWidget() {
	m_game = game;
	QGridLayout *layout = new QGridLayout(this);
	m_moveBox = new MoveBox;
	m_simulateButton = new QPushButton(tr("Simulate"));
	layout->setAlignment(Qt::AlignTop);
	layout->addWidget(m_simulateButton, 0, 0);
	layout->addWidget(m_moveBox, 1, 0);
	const char *home = getenv("HOME");
	// TODO: configurable path
	std::string execPath = home ? home : "/";
	execPath += "/apps/macondo/macondo";
	initOptions = std::make_unique<MacondoInitOptions>(execPath);
	m_backend = new MacondoBackend(game, *initOptions);
	connect(m_simulateButton, SIGNAL(clicked()), this, SLOT(simulate()));
	connect(m_backend, SIGNAL(gotSimMoves(const Quackle::MoveList &)), this, SLOT(gotSimMoves(const Quackle::MoveList &)));
}

Macondo::~Macondo() {}

void Macondo::simulate() {
	MacondoSimulateOptions options;
	m_backend->simulate(options);
	m_moveBox->positionChanged(&m_game->currentPosition());
}


void Macondo::setGame(Quackle::Game *game) {
	delete m_backend;
	m_backend = new MacondoBackend(game, *initOptions);
	m_game = game;
}

void Macondo::gotSimMoves(const Quackle::MoveList &moves) {
	m_moveBox->movesChanged(&moves);
	/*printf("got %zu moves\n",moves.size());
	for (const Quackle::Move &move: moves) {
		std::cout << move << std::endl;
	}
	*/
}
