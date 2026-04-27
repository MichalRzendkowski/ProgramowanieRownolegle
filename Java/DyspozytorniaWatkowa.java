import java.util.LinkedList;
import java.util.Set;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class DyspozytorniaWatkowa implements Dyspozytornia {

    private Map<Integer, Taxi> taksowki = new HashMap<>();

    private Deque<Integer> wolne = new LinkedList<>();
    private Deque<Integer> zlecenia = new LinkedList<>();
    private Deque<Integer> zleceniaPriorytet = new LinkedList<>();

    private AtomicInteger nastepnyNumerZlecenia = new AtomicInteger(0);
    private AtomicInteger zepsuta = new AtomicInteger(-1);
    private AtomicBoolean running = new AtomicBoolean(true);

    Object semafor = new Object();

    Thread wykonywaczZlecen = new Thread(new Runnable() {
        public void run() {
            while (running.get()) {
                int wolnaID;
                int zlecenieID;
                Taxi wolna;

                synchronized (semafor) {
                    while (running.get() && ((zlecenia.isEmpty() && zleceniaPriorytet.isEmpty())|| wolne.isEmpty())) {
                        try {
                            semafor.wait();
                        } catch (InterruptedException e) {
                        }
                    }

                    if (!running.get()) break;

                    wolnaID = wolne.poll();
                    if(zleceniaPriorytet.isEmpty()){
                        zlecenieID = zlecenia.poll();
                    }else{
                        zlecenieID = zleceniaPriorytet.poll();
                    }
                    
                    wolna = taksowki.get(wolnaID);
                }

                Thread th = new Thread(() -> {
                    int id = wolnaID;
                    int zlecenie = zlecenieID;

                    try {
                        wolna.wykonajZlecenie(zlecenie);
                    } finally {
                        synchronized (semafor) {
                            if (zepsuta.get() != id && !wolne.contains(id)) {
                                wolne.add(id);
                            }
                            semafor.notifyAll();
                        }
                    }
                });
                th.setDaemon(true);
                th.start();
            }

        }
    });

    public DyspozytorniaWatkowa() {
    }

    public void flota(Set<Taxi> flota) {
        for (Taxi taxi : flota) {
            int numer = taxi.numer();
            taksowki.put(numer, taxi);
            wolne.add(numer);
        }
        wykonywaczZlecen.setDaemon(true);
        wykonywaczZlecen.start();
    }

    public int zlecenie() {
        int numerZlecenia = nastepnyNumerZlecenia.incrementAndGet();
        synchronized (semafor) {
            zlecenia.add(numerZlecenia);
            semafor.notifyAll();
        }
        return numerZlecenia;
    }

    public void awaria(int numer, int numerZlecenia) {
        synchronized (semafor) {
            zepsuta.set(numer);
            wolne.remove(numer);
            zleceniaPriorytet.add(numerZlecenia);
            semafor.notifyAll();
        }
    }

    public void naprawiono(int numer) {
        synchronized (semafor) {
            if (zepsuta.get() == numer) {
                if(! wolne.contains(numer)) wolne.add(numer);
                zepsuta.set(-1);
            }
            semafor.notifyAll();
        }
    }

    public Set<Integer> koniecPracy() {
        running.set(false);
        synchronized(semafor){
            semafor.notifyAll();
        }
        Set<Integer> outPriorytet = new HashSet<>(zleceniaPriorytet);
        Set<Integer> out = new HashSet<>(zlecenia);
        Set<Integer> combined = Stream.concat(outPriorytet.stream(), out.stream()).collect(Collectors.toSet());
        return new HashSet<Integer>(combined);
    }
}