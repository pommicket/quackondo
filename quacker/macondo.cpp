#include "macondo.h"
#include "macondobackend.h"
#include "quacker.h"

#include <QGridLayout>
#include <QPushButton>

Macondo::Macondo(Quackle::Game *game) : QWidget() {
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
	connect(m_backend, SIGNAL(gotSimMoves(const std::vector<Quackle::Move> &)), this, SLOT(gotSimMoves(const std::vector<Quackle::Move> &)));
}

void Macondo::simulate() {
	MacondoBackend::SimulateOptions options;
	m_backend->simulate(options);
}

void Macondo::gotSimMoves(const std::vector<Quackle::Move> &moves) {
	printf("got %zu moves\n",moves.size());
	for (const Quackle::Move &move: moves) {
		std::cout << move << std::endl;
	}
}
