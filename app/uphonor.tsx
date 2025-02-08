import m from 'mithril'

export default {
	initial: {
		loops: [],
	},
	services: [],
	view: (cell) => [
		m('h1', 'μPhonor'),
		m('p', 'A Micro Holophonor!'),
		m(
			'button',
			{
				onclick: () => {
					let loops = cell.getState().loops
					cell.update({ loops: [...loops, `loop ${loops.length + 1}`] })
				},
			},
			'Add Loop'
		),
		cell.state.loops.map((loop) => m('p', loop)),
	],
}
