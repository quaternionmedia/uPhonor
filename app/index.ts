import m from 'mithril'
import meiosisTracer from 'meiosis-tracer'
import { meiosisSetup } from 'meiosis-setup'
import uPhonor from './uphonor'
export interface State {}

// Initialize Meiosis
const cells = meiosisSetup<State>({ app: uPhonor })

m.mount(document.getElementById('app'), {
	view: () => uPhonor.view(cells()),
})

cells.map((state) => {
	console.log('cells', state)

	//   Persist state to local storage
	//   localStorage.setItem('meiosis', JSON.stringify(state))
	// m.redraw()
})

// Debug

meiosisTracer({
	selector: '#tracer',
	rows: 25,
	streams: [cells],
})

window.cells = cells
